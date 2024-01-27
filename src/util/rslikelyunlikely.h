/*******************************************************************************
 * RetroShare C++20 likely/unlikely backwards compatibility utilities          *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2023  Gioacchino Mazzurco <gio@retroshare.cc>                 *
 * Copyright (C) 2023  Asociación Civil Altermundi <info@altermundi.net>       *
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

#if __cplusplus >= 202002L
#	define RS_LIKELY [[likely]]
#	define RS_UNLIKELY [[unlikely]]
#else // __cplusplus >= 202002L
#	define RS_LIKELY
#	define RS_UNLIKELY
#endif // __cplusplus >= 202002L
