/*******************************************************************************
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2022  Gioacchino Mazzurco <gio@altermundi.net>                *
 * Copyright (C) 2022  Asociación Civil Altermundi <info@altermundi.net>       *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#pragma once

/** Comfortable optional variadic macro arguments
 * @see https://www.appsloveworld.com/cplus/100/16/variadic-macros-with-zero-arguments
 * C/C++ variadic macros gives error on expansion if no argument is passed,
 * the result is that at least one argument must be passed or compilation fails.
 * Wrapping __VA_ARGS__ in RS_OPT_VA_ARGS at expansion place omitting the comma
 * before solves this issue rendering the variadic arguments effectively
 * optionals.
 */
#define RS_OPT_VA_ARGS(...) , ##__VA_ARGS__

/**
 * Concatenate preprocessor tokens A and B without expanding macro definitions
 * (however, if invoked from a macro, macro arguments are expanded).
 */
#define RS_CONCAT_MACRO_NX(A, B) A ## B

/// Concatenate preprocessor tokens A and B after macro-expanding them.
#define RS_CONCAT_MACRO(A, B) RS_CONCAT_MACRO_NX(A, B)
