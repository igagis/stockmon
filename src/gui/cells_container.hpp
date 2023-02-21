/*
beerja - stock screener

Copyright (C) 2020-2021  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* ================ LICENSE END ================ */

#pragma once

#include <morda/widgets/group/column.hpp>

namespace beerja{

class cells_container :
        virtual public morda::widget,
        private morda::column
{
    unsigned num_cells_per_row = 3;
public:
    cells_container(utki::shared_ref<morda::context> c, const treeml::forest& desc);

    void set_num_cells_per_row(unsigned num);

	void push(utki::shared_ref<morda::widget> w);

	void erase(morda::widget& w);
private:
	void update_arrangement();
	morda::container& push_new_row();
};

}
