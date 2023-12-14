/*******************************************************************************
 * RetroShare debugging utilities                                              *
 *                                                                             *
 * Copyright (C) 2022-2023  Gioacchino Mazzurco <gio@retroshare.cc>            *
 * Copyright (C) 2022-2023  Asociación Civil Altermundi <info@altermundi.net>  *
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

#include <system_error>
#include <type_traits>

#include "util/rslikelyunlikely.h"
#include "util/rsdebug.h"
#include "util/stacktrace.h"


/**
 * @def rs_error_bubble_or_exit
 * Bubble up an error condition to be handled upstream if possible or deal with
 * it fatally here, is a very common pattern, @see rs_malloc as an example, so
 * instead of rewriting the same snippet over and over, increasing the
 * possibility of introducing bugs, use this macro to properly deal with that
 * situation.
 * FIRST PARAM std::error_condition occurred to be dealt with
 * SECOND PARAM std::error_condition* pointer to a location to store the
 *	std::error_condition to be bubbled up upstream, if it is nullptr the error
 *	will be handled with a fatal report end then exiting here
 * EXTRA PARAMS ... optional additional information you want to be printed
 * toghether with the error report when is fatal (aka not bubbled up) */
#define rs_error_bubble_or_exit(... ) \
	rsErrorBubbleOrExitDebuggable(__PRETTY_FUNCTION__, __VA_ARGS__)


/** @brief Do not call directly use @see rs_error_bubble_or_exit instead.
 * Implemented as a function for debugger ergonomy */
template <typename PF, typename... Args>
void rsErrorBubbleOrExitDebuggable(
        PF prettyCaller,
        const std::error_condition& errorCondition,
        std::error_condition* bubbleStorage,
        Args&&... args )
{
	/* Enforce __PRETTY_FUNCTION__ or something similar is passed as first
	 * paramether.
	 * std::decay is needed to erase array lenght from the type, otherwise
	 * because __PRETTY_FUNCTION__ has different lenght for each function the
	 * type would not match and the static_assert fail. */
	static_assert(
	    std::is_same<
		        typename std::decay_t<PF>,
		        typename std::decay_t<decltype(__PRETTY_FUNCTION__)> >::value );

	if(!errorCondition) RS_UNLIKELY
	{
		/* This is an unexpected situation, added to help debugging */
		RS_ERR("Called without error information!");
		print_stacktrace();
		return;
	}

	if(bubbleStorage)
	{
		*bubbleStorage = errorCondition;
	}
	else
	{
		RsFatal(prettyCaller, " ", errorCondition, " ", args...);
		print_stacktrace();
		exit(std::error_condition(errorCondition).value());
	}
}
