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

// The Indicator class provides flags with different levels from 0 to n-1.
//
// Flags can be set at a specific level, and checked at all levels up to some
// given level. As a consequence, it is possible to use these flags to conduct actions
// at different priority levels: 0 has lowest priority, n-1 is highest.

class Indicator
{
public:
    explicit Indicator(uint16_t n = 1)
            : changeFlags(n)
    {
        IndicateChanged();
    }

    /*!
     * \brief IndicateChanged
     * 			Sets all levels to 1.
     */
    void	IndicateChanged()
    {
        for(uint16_t i = 0; i < changeFlags.size(); i++)
            changeFlags[i]=true;
    }

    /*!
     * \brief Reset
     * 			Resets all flags.
     */
    void Reset()
    {
        for(uint32_t i=0;i<changeFlags.size();++i)
            changeFlags[i]=false;
    }
    /*!
     * \brief IndicateChanged
     * 			Sets all levels up to level l. This reflects the fact that when checking,
     *          any check that tests for lower urgency (meaning for less urgent business) needs to know that
     *          a change has been made, so as to avoid other loops for more urgent business to also save.
     * \param l
     */
    void	IndicateChanged(int l)
    {
        for(uint16_t i = 0; i <= l ; i++)
            changeFlags[i]=true;
    }

    /*!
     * \brief Changed
     * 				Checks whether level idx or below has been changed, and reset *all*.
     * 				This reflects the fact that once a level is positively checked, all levels
     * 				need to be reset since the action does not need to be done again whatever its
     * 				priority.
     * \param idx
     * \return
     */
    bool Changed(uint16_t idx = 0)
    {
        /* catch overflow */

        bool ans = changeFlags[idx];

        if(ans)
            for(uint32_t i=0;i<changeFlags.size();++i)
                changeFlags[i] = false;

        return ans;
    }

private:
    std::vector<bool> changeFlags;
};



#endif
