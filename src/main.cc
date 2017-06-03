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
            "Mostly for testing:\n"
            "\t-S         : Skip sled loading; assume board already loaded.\n"
            "\t-E         : Skip eject at end.\n"
            "\t-F         : Run a focus round until Ctrl-C\n"
            "\t-M         : Testing: Inhibit sled move.\n"
            "\t-n         : Dryrun. Do not do any scanning; laser off.\n"
            "\t-j<exp>    : Mirror jitter test with given exposure repeat\n"
            "\t-h         : This help\n");
    return errmsg ? 1 : 0;
}

// Given an image filename, create a LDGraphyScanner that can be used to expose
// that image.
bool LoadImage(LDGraphyScanner *scanner,
               const char *filename, float override_dpi, bool invert) {
    if (!filename) return false;
    double input_dpi = -1;
    SimpleImage *img = LoadPNGImage(filename, &input_dpi);
    if (img == nullptr)
        return false;

    if (override_dpi > 0 || input_dpi < 100 || input_dpi > 20000)
        input_dpi = override_dpi;

    if (input_dpi < 100 || input_dpi > 20000) {
        fprintf(stderr, "Couldn't extract usable DPI from image. "
                "Please provide -d <dpi>\n");
        return false;
    }

    ConvertBlackWhite(img, 128, invert);

    scanner->SetImage(img, input_dpi);
    return true;
}

// Output a line with dots in regular distance for testing the set-up.
void RunFocusLine(LDGraphyScanner *scanner) {
    // Essentially, we want a one-line image of known resolution with regular
    // pixels set.
    constexpr int bed_width = 100;   // 100 mm bed
    constexpr int res = 10;          // 1/10 mm resolution
    constexpr int mark_interval = 5; // every 5 mm
    SimpleImage *img = new SimpleImage(1, bed_width * res);
    for (int mm = 0; mm < bed_width; mm += mark_interval) {
        img->at(0, mm * res) = 255;
    }
    const float dpi = 1000.0 / (100.0 / 25.4);  // 10 dots per mm
    scanner->SetImage(img, dpi);
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
    int mirror_adjust_exposure = 0;
    float offset_x = 0;
    float exposure_factor = 1.0f;

    int opt;
    while ((opt = getopt(argc, argv, "MFhnid:x:j:o:SE")) != -1) {
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
        }
    }

    const char *filename = nullptr;

    if (argc > optind+1)
        return usage(argv[0], "Exactly one image file expected");
    if (argc == optind + 1) {
        filename = argv[optind];
    }

    if (!filename && !do_focus && !mirror_adjust_exposure)
        return usage(argv[0]);   // Nothing to do.

    if (exposure_factor < 1.0f) {
        return usage(argv[0], "Exposure factor needs to be at least 1.");
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

    ScanLineSender *line_sender = dryrun
        ? new DummyScanLineSender()
        : PRUScanLineSender::Create();

    if (!line_sender) {
        fprintf(stderr, "Cannot initialize hardware.\n");
        return 1;
    }

    ArmInterruptHandler();  // While PRU running, we want controlled exit.
    LDGraphyScanner *ldgraphy = new LDGraphyScanner(line_sender,
                                                    exposure_factor);

    if (mirror_adjust_exposure) {
        ldgraphy->ExposeJitterTest(6, mirror_adjust_exposure);
    }

    if (do_focus) {
        fprintf(stderr, "== FOCUS run. Exit with Ctrl-C. ==\n");
        RunFocusLine(ldgraphy);
    }

    if (LoadImage(ldgraphy, filename, commandline_dpi, invert)) {
        fprintf(stderr, "== Exposure. Emergency stop with Ctrl-C. ==\n");
        fprintf(stderr, "Estimated time: %.0f seconds\n",
                ldgraphy->estimated_time_seconds());
        float prev_percent = -1;
        ldgraphy->ScanExpose(
            do_move, [&prev_percent](int done, int total) {
                // Simple commandline progress indicator.
                const int percent = roundf(100.0 * done / total);
                if (percent != prev_percent) {
                    fprintf(stderr, "\b\b\b\b\b\b\b\b\b\b\b\b%d%% (%d)",
                            percent, done);
                    fflush(stderr);
                    prev_percent = percent;
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
