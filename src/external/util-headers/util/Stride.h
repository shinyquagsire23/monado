/**	@file
	@brief	A class for handling "do this every n times"

	This header is maintained as a part of 'util-headers' - you can always
    find the latest version online at https://github.com/rpavlik/util-headers

    This GUID can help identify the project: d1dbc94e-e863-49cf-bc08-ab4d9f486613

    This copy of the header is from the revision that Git calls
    1a8444782d15cb9458052e3d8251c4f5b8e808d5

    Commit date: "2022-03-11 12:11:32 -0600"

	@date
	2009-2010

	@author
	Ryan Pavlik
	<rpavlik@iastate.edu> and <abiryan@ryand.net>
	http://academic.cleardefinition.com/
	Iowa State University Virtual Reality Applications Center
	Human-Computer Interaction Graduate Program
*/

// Copyright 2009-2010, Iowa State University.
//
// SPDX-License-Identifier: BSL-1.0
//
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#ifndef INCLUDED_Stride_h_GUID_eaa50b9c_e526_4656_89dc_99008d82447d
#define INCLUDED_Stride_h_GUID_eaa50b9c_e526_4656_89dc_99008d82447d

// Local includes
// - none

// Library includes
// - none

// Standard includes
// - none

namespace util {

/// @addtogroup Other Other Utility Classes
/// @{

/// Handle the task of "do this every n times" in an easy way.
	class Stride {
		public:
			Stride(const unsigned int n) :
				_stride(n),
				_step(0) { }

			void advance() {
				_step = (_step + 1) % _stride;
			}

			Stride operator++() {
				Stride temp = *this;
				advance();
				return temp;
			}

			Stride & operator++(int) {
				advance();
				return *this;
			}

			operator bool() const {
				return _step == 0;
			}

		private:
			unsigned int _stride;
			unsigned int _step;
	};

/// @}

} // end of util namespace

#endif // INCLUDED_Stride_h_GUID_eaa50b9c_e526_4656_89dc_99008d82447d

