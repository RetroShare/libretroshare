/*******************************************************************************
 * libretroshare/src/pqi: pqiindic.h                                           *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2004-2006 by Robert Fernie <retroshare@lunamutt.com>              *
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
#ifndef MRK_PQI_INDICATOR_HEADER
#define MRK_PQI_INDICATOR_HEADER

#include <vector>
#include <stdint.h>
#include <assert.h>

// The Indicator class provides flags with different levels from 0 to 31.
//
// Flags can be set at a specific level, and checked at all levels up to some
// given level. As a consequence, it is possible to use these flags to conduct actions
// at different priority levels: 0 has lowest priority, 31 has highest.

class Indicator
{
public:
    explicit Indicator()
            : changeFlags(0)
    {
        IndicateChanged();
    }

    /*!
     * \brief IndicateChanged
     * 			Sets all levels to 1.
     */
    void	IndicateChanged()
    {
        changeFlags=~0;
    }

    /*!
     * \brief Reset
     * 			Resets all flags.
     */
    void Reset()
    {
        changeFlags=0;
    }
    /*!
     * \brief IndicateChanged
     * 			Sets all levels up to level l. This reflects the fact that when checking,
     *          any check that tests for lower urgency (meaning for less urgent business) needs to know that
     *          a change has been made, so as to avoid other loops for more urgent business to also save.
     * \param l
     */
    void	IndicateChanged(uint8_t l)
    {
        assert(l < 31);
        changeFlags |= (1u << (l+1))-1;
    }

    /*!
     * \brief Changed
     * 				Checks whether level idx or below has been changed, and reset *all* levels.
     * 				This reflects the fact that once a level is positively checked, all levels
     * 				need to be reset since the action is considered done.
     * \param idx
     * \return
     */
    bool Changed(uint8_t idx = 0)
    {
        /* catch overflow */
        assert(idx < 32);

        bool ans(changeFlags & (1u << idx));

        if(ans)
            changeFlags = 0;

        return ans;
    }

private:
    uint32_t changeFlags;
};



#endif
