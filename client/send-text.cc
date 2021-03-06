// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// Copyright (C) 2016 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "udp-flaschen-taschen.h"

#include "bdf-font.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

static int usage(const char *progname) {
    fprintf(stderr, "usage: %s [options] <TEXT>\n", progname);
    fprintf(stderr, "Options:\n"
            "\t-g <width>x<height>[+<off_x>+<off_y>[+<layer>]] : Output geometry. Default 45x<font-height>+0+0+1\n"
            "\t-l <layer>      : Layer 0..15. Default 1 (note if also given in -g, then last counts)\n"
            "\t-h <host>       : Flaschen-Taschen display hostname.\n"
            "\t-f <fontfile>   : Path to *.bdf font file\n"
            "\t-s<ms>          : Scroll milliseconds per pixel (default 60). 0 for no-scroll.\n"
            "\t-o              : Only run once, don't scroll forever.\n"
            "\t-c<RRGGBB>      : Text color as hex (default: FFFFFF)\n"
            "\t-b<RRGGBB>      : Background color as hex (default: 000000)\n"
            );

    return 1;
}

int main(int argc, char *argv[]) {
    int width = 45;
    int height = -1;
    int off_x = 0;
    int off_y = 0;
    int off_z = 1;
    int scroll_delay_ms = 50;
    bool run_forever = true;
    const char *host = NULL;

    Color fg(0xff, 0xff, 0xff);
    Color bg(0, 0, 0);
    int r, g, b;

    ft::Font font;
    int opt;
    while ((opt = getopt(argc, argv, "f:g:h:s:oc:b:l:")) != -1) {
        switch (opt) {
        case 'g':
            if (sscanf(optarg, "%dx%d%d%d%d", &width, &height, &off_x, &off_y, &off_z)
                < 2) {
                fprintf(stderr, "Invalid size spec '%s'", optarg);
                return usage(argv[0]);
            }
            break;
        case 'h':
            host = strdup(optarg); // leaking. Ignore.
            break;
        case 'f':
            if (!font.LoadFont(optarg)) {
                fprintf(stderr, "Couldn't load font '%s'\n", optarg);
            }
            break;
        case 'o':
            run_forever = false;
            break;
        case 'l':
            off_z = atoi(optarg);
            break;
        case 's':
            scroll_delay_ms = atoi(optarg);
            if (scroll_delay_ms > 0 && scroll_delay_ms < 10) {
                // Don't do crazy packet sending.
                scroll_delay_ms = 10;
            }
            break;
        case 'c':
            if (sscanf(optarg, "%02x%02x%02x", &r, &g, &b) != 3) {
                fprintf(stderr, "Foreground color parse error\n");
                return usage(argv[0]);
            }
            fg.r = r; fg.g = g; fg.b = b;
            break;
        case 'b':
            if (sscanf(optarg, "%02x%02x%02x", &r, &g, &b) != 3) {
                fprintf(stderr, "Background color parse error\n");
                return usage(argv[0]);
            }
            bg.r = r; bg.g = g; bg.b = b;
            break;

        default:
            return usage(argv[0]);
        }
    }

    if (font.height() < 0) {
        fprintf(stderr, "Need to provide a font.\n");
        return usage(argv[0]);
    }

    if (height < 0) {
        height = font.height();
    }

    if (width < 1 || height < 1) {
        fprintf(stderr, "%dx%d is a rather unusual size\n", width, height);
        return usage(argv[0]);
    }

    int fd = OpenFlaschenTaschenSocket(host);
    if (fd < 0) {
        fprintf(stderr, "Cannot connect.\n");
        return 1;
    }

    UDPFlaschenTaschen display(fd, width, height);
    display.SetOffset(off_x, off_y, off_z);

    // Assemble all non-option arguments to one text.
    std::string str;
    for (int i = optind; i < argc; ++i) {
        str.append(argv[i]).append(" ");
    }
    const char *text = str.c_str();

    // Eat leading spaces and figure out if we end up with any
    // text at all.
    while (isspace(*text)) {
        ++text;
    }
    if (!*text) {
        fprintf(stderr, "This looks like a very empty text.\n");
        return 1;
    }

    // Center in in the available display space.
    const int y_pos = (height - font.height()) / 2 + font.baseline();

    // dry-run to determine total number of pixels.
    const int total_len = DrawText(&display, font, 0, y_pos, fg, NULL, text);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    if (scroll_delay_ms > 0) {
        do {
            display.Fill(bg);
            for (int s = 0; s < total_len + width && !interrupt_received; ++s) {
                DrawText(&display, font, width - s, y_pos, fg, &bg, text);
                display.Send();
                usleep(scroll_delay_ms * 1000);
            }
        } while (run_forever && !interrupt_received);
    }
    else {
        // No scrolling, just show directly and once.
        display.Fill(bg);
        DrawText(&display, font, 0, y_pos, fg, &bg, text);
        display.Send();
    }

    // Don't let leftovers cover up content.
    if (off_z > 0 && interrupt_received) {
        display.Clear();
        display.Send();
    }

    close(fd);

    if (interrupt_received) {
        fprintf(stderr, "Interrupted. Exit.\n");
    }
    return 0;
}
