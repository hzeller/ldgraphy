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

static int usage(const char *progname, const char *errmsg = NULL) {
    if (errmsg) {
        fprintf(stderr, "\n%s\n\n", errmsg);
    }
    fprintf(stderr, "Usage:\n%s [options] <png-image-file>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-d <val>   : Override DPI of input image. Default -1\n"
            "\t-i         : Inverse image: black becomes laser on\n"
            "\t-M         : Inhibit move in x direction\n"
            "\t-F         : Run a focus round until Ctrl-C\n"
            "\t-n         : Dryrun. Do not do any scanning.\n"
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
        scanner->ScanExpose(false, [](int, int) { return !is_interrupted(); });
    }
    fprintf(stderr, "Focus run done.\n");
}

int main(int argc, char *argv[]) {
    double commandline_dpi = -1;
    bool dryrun = false;
    bool invert = false;
    bool do_focus = false;
    bool do_move = true;

    int opt;
    while ((opt = getopt(argc, argv, "MFhnid:")) != -1) {
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
        }
    }

    const char *filename = nullptr;

    if (argc > optind+1)
        return usage(argv[0], "Exactly one image file expected");
    if (argc == optind + 1) {
        filename = argv[optind];
    }

    if (!filename && !do_focus)
        return usage(argv[0]);   // Nothing to do.

    ScanLineSender *line_sender = dryrun
        ? new DummyScanLineSender()
        : PRUScanLineSender::Create();
    if (!line_sender) {
        fprintf(stderr, "Cannot initialize hardware.\n");
        return 1;
    }

    LDGraphyScanner ldgraphy(line_sender);

    if (do_focus) {
        fprintf(stderr, "== FOCUS run. Exit with Ctrl-C. ==\n");
        RunFocusLine(&ldgraphy);
    }

    if (LoadImage(&ldgraphy, filename, commandline_dpi, invert)) {
        ArmInterruptHandler();
        fprintf(stderr, "== Exposure. Emergency stop with Ctrl-C. ==\n");
        fprintf(stderr, "Estimated time: %.0f seconds\n",
                ldgraphy.estimated_time_seconds());
        float prev_percent = -1;
        ldgraphy.ScanExpose(
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

    return 0;
}
