/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef X86_EMU_PANIC_H
#define X86_EMU_PANIC_H

#define VM_PANIC(x) {\
    printf("%s\n", x); \
    abort(); \
}

#define VM_PANIC_ON(x) {\
    if (x) { \
        printf("%s\n", #x); \
        abort(); \
    } \
}

#define VM_PANIC_EX(...) {\
    printf(__VA_ARGS__); \
    abort(); \
}

#define VM_PANIC_ON_EX(x, ...) {\
    if (x) { \
        printf(__VA_ARGS__); \
        abort(); \
    } \
}

#endif
