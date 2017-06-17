/* -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * (c) 2017 Henner Zeller <h.zeller@acm.org>
 *
 * This file is part of LDGraphy http://github.com/hzeller/ldgraphy
 *
 * LDGraphy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LDGraphy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LDGraphy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include "containers.h"
#include "image-processing.h"
#include "laser-scribe-constants.h"
#include "ldgraphy-scanner.h"
#include "scanline-sender.h"
#include "sled-control.h"

constexpr float kThinningChartResolution = 0.005; // mm per pixel

// Interrupt handling. Provide a is_interrupted() function that reports
// if Ctrl-C has been pressed. Requires ArmInterruptHandler() called before use.
bool s_handler_installed = false;
volatile bool s_interrupt_received = false;
static void InterruptHandler(int) {
  s_interrupt_received = true;
}
static void ArmInterruptHandler() {
    if (!s_handler_installed) {
        signal(SIGTERM, InterruptHandler);
        signal(SIGINT, InterruptHandler);
        s_handler_installed = true;
    }
    s_interrupt_received = false;
}
static bool is_interrupted() { return s_interrupt_received; }
static void DisarmInterruptHandler() {
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
}

static int usage(const char *progname, const char *errmsg = NULL) {
    if (errmsg) {
        fprintf(stderr, "\n%s\n\n", errmsg);
    }
    fprintf(stderr, "Usage:\n%s [options] <png-image-file>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-d <val>   : Override DPI of input image. Default -1\n"
            "\t-i         : Inverse image: black becomes laser on\n"
            "\t-x<val>    : Exposure factor. Default 1.\n"
            "\t-o<val>    : Offset in sled direction in mm\n"
            "\t-R         : Quarter image turn left; "
            "can be given multiple times.\n"
            "\t-h         : This help\n"
            "Mostly for testing or calibration:\n"
            "\t-S         : Skip sled loading; assume board already loaded.\n"
            "\t-E         : Skip eject at end.\n"
            "\t-F         : Run a focus round until Ctrl-C\n"
            "\t-M         : Testing: Inhibit sled move.\n"
            "\t-n         : Dryrun. Do not do any scanning; laser off.\n"
            "\t-j<exp>    : Mirror jitter test with given exposure repeat\n"
            "\t-D<line-width:start,step> : Laser Dot Diameter test chart. Creates a test-strip 10cm x 2cm with 10 samples.\n");
    return errmsg ? 1 : 0;
}

// Given an image filename, create a LDGraphyScanner that can be used to expose
// that image.
bool LoadImage(LDGraphyScanner *scanner,
               const char *filename, float override_dpi,
               bool invert, int quarter_turns) {
    if (!filename) return false;
    double input_dpi = -1;
    std::unique_ptr<BitmapImage> img(LoadPNGImage(filename, invert, &input_dpi));
    if (img == nullptr) return false;

    if (override_dpi > 0 || input_dpi < 100 || input_dpi > 20000)
        input_dpi = override_dpi;

    if (input_dpi < 100 || input_dpi > 20000) {
        fprintf(stderr, "Couldn't extract usable DPI from image. "
                "Please provide -d <dpi>\n");
        return false;
    }

    while (quarter_turns--)
        img.reset(CreateRotatedImage(*img));

    return scanner->SetImage(img.release(), 25.4 / input_dpi);
}

// Output a line with dots in regular distance for testing the set-up.
void RunFocusLine(LDGraphyScanner *scanner) {
    // Essentially, we want a one-line image of known resolution with regular
    // pixels set.
    constexpr int bed_width = 100;   // 100 mm bed
    constexpr int res = 10;          // 1/10 mm resolution
    constexpr int mark_interval = 5; // every 5 mm
    BitmapImage *img = new BitmapImage(1, bed_width * res);
    for (int mm = 0; mm < bed_width; mm += mark_interval) {
        img->Set(0, mm * res, true);
    }
    scanner->SetImage(img, 0.01);
    ArmInterruptHandler();
    while (!is_interrupted()) {
        if (!scanner->ScanExpose(false,
                                 [](int, int) { return !is_interrupted(); })) {
            break;
        }
    }
    fprintf(stderr, "Focus run done.\n");
}

void UIMessage(const char *msg) {
    fprintf(stdout, "**********> %s\n", msg);
}

int main(int argc, char *argv[]) {
    double commandline_dpi = -1;
    bool dryrun = false;
    bool invert = false;
    bool do_focus = false;
    bool do_move = true;
    bool do_sled_loading_ui = true;
    bool do_sled_eject = true;
    std::unique_ptr<BitmapImage> dot_size_chart;

    int quarter_turns = 0;
    int mirror_adjust_exposure = 0;
    float offset_x = 0;
    float exposure_factor = 1.0f;

    int opt;
    while ((opt = getopt(argc, argv, "MFhnid:x:j:o:SERD:")) != -1) {
        switch (opt) {
        case 'h': return usage(argv[0]);
        case 'd':
            commandline_dpi = atof(optarg);
            break;
        case 'n':
            dryrun = true;
            break;
        case 'i':
            invert = true;
            break;
        case 'F':
            do_focus = true;
            break;
        case 'M':
            do_move = false;
            break;
        case 'x':
            exposure_factor = atof(optarg);
            break;
        case 'j':
            mirror_adjust_exposure = atoi(optarg);
            break;
        case 'o':
            offset_x = atof(optarg);   // TODO: also y. as x,y coordinate.
            break;
        case 'S':
            do_sled_loading_ui = false;
            break;
        case 'E':
            do_sled_eject = false;
            break;
        case 'R':
            quarter_turns++;
            break;
        case 'D': {
            float line_w, start, step;
            if (sscanf(optarg, "%f:%f,%f", &line_w, &start, &step) == 3) {
                dot_size_chart.reset(
                    CreateThinningTestChart(kThinningChartResolution,
                                            line_w, 10, start, step));
            } else {
                return usage(argv[0], "Invalid Laser dot diameter chart params");
            }
            break;
        }

        }
    }

    const char *filename = nullptr;

    if (argc > optind+1)
        return usage(argv[0], "Exactly one image file expected");
    if (argc == optind + 1) {
        filename = argv[optind];
    }

    if (exposure_factor < 1.0f) {
        return usage(argv[0], "Exposure factor needs to be at least 1.");
    }

    if (filename && dot_size_chart) {
        return usage(argv[0], "You can either expose an image or create a "
                     "dot size chart, but not both.");
    }

    if (!filename && !do_focus && !mirror_adjust_exposure && !dot_size_chart)
        return usage(argv[0]);   // Nothing to do.

    bool do_image = false;
    LDGraphyScanner *ldgraphy = new LDGraphyScanner(exposure_factor);
    if (dot_size_chart) {
        do_image = true;
        ldgraphy->SetLaserDotSize(0, 0);  // Chart already thinned image.
        ldgraphy->SetImage(dot_size_chart.release(), kThinningChartResolution);
    } else {
        do_image = LoadImage(ldgraphy, filename,
                             commandline_dpi, invert, quarter_turns  % 4);
        if (filename && !do_image) return 1;  // Got file, but failed loading.
    }

    if (do_image) {
        fprintf(stderr, "Estimated time: %.0f seconds\n",
                ldgraphy->estimated_time_seconds());
    }

    SledControl sled(4000, do_move && !dryrun);

    // Super-crude UI
    if (do_sled_loading_ui) {
        UIMessage("Hold on .. sled to take your board is on the way...");
        sled.Move(180);  // Move all the way out for person to place device.
        UIMessage("Here we are. Please place board in (0,0) corner. Press <RETURN>.");
        while (fgetc(stdin) != '\n')
            ;
        UIMessage("Thanks. Getting ready to scan.");
    }

    sled.Move(-180);   // Back to base.

    float forward_move = 3;  // Forward until we reach begin.
    if (mirror_adjust_exposure) forward_move += 5;
    forward_move += offset_x;
    sled.Move(forward_move);

    ArmInterruptHandler();  // While PRU running, we want controlled exit.
    ScanLineSender *line_sender = dryrun
        ? new DummyScanLineSender()
        : PRUScanLineSender::Create();
    if (!line_sender) {
        fprintf(stderr, "Cannot initialize hardware.\n");
        return 1;
    }

    ldgraphy->SetScanLineSender(line_sender);

    if (mirror_adjust_exposure) {
        ldgraphy->ExposeJitterTest(6, mirror_adjust_exposure);
    }

    if (do_focus) {
        fprintf(stderr, "== FOCUS run. Exit with Ctrl-C. ==\n");
        RunFocusLine(ldgraphy);
    }

    if (do_image) {
        fprintf(stderr, "== Exposure. Emergency stop with Ctrl-C. ==\n");
        int prev_percent = -1, prev_remain_time = -1;
        const float total_sec = ldgraphy->estimated_time_seconds();
        ldgraphy->ScanExpose(
            do_move,
            [&prev_percent, &prev_remain_time, total_sec](int done, int total) {
                // Simple commandline progress indicator.
                const int percent = roundf(100.0 * done / total);
                const int remain_time = roundf(total_sec -
                                               (total_sec * done / total));
                // Only update if any number would change.
                if (percent != prev_percent || remain_time != prev_remain_time) {
                    fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
                            "%3d%%; %d:%02d left",
                            percent, remain_time / 60, remain_time % 60);
                    fflush(stderr);
                    prev_percent = percent;
                    prev_remain_time = remain_time;
                }
                return !is_interrupted();
            });
        if (is_interrupted())
            fprintf(stderr, "Interrupted. Exposure might be incomplete.\n");
    }

    delete ldgraphy;  // First make PRU stop using our pins.

    DisarmInterruptHandler();   // Everything that comes now: fine to interrupt

    if (do_sled_eject) {
        UIMessage("Done Scanning - sending the sled with the board towards you.");
        sled.Move(180);  // Move out for user to grab.

        UIMessage("Here we are. Please take the board and press <RETURN>");
        // TODO: here, when the user takes too long, just pull in board again
        // to have it more protected against light.
        while (fgetc(stdin) != '\n')
            ;
        UIMessage("Thanks. Going back.");
        sled.Move(-85);
    }

    return 0;
}
