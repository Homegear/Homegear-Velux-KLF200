/* Copyright 2013-2019 Homegear GmbH
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#ifndef GD_H_
#define GD_H_

#define VELUX_FAMILY_ID 27
#define VELUX_FAMILY_NAME "Velux KLF200"

#include <homegear-base/BaseLib.h>

using namespace BaseLib;
using namespace BaseLib::DeviceDescription;

namespace Velux
{

class Velux;
class Klf200;

class GD
{
public:
	virtual ~GD();

	static BaseLib::SharedObjects* bl;
	static Velux* family;
	static BaseLib::Output out;
    static std::map<std::string, std::shared_ptr<Klf200>> physicalInterfaces;
    static std::shared_ptr<Klf200> defaultPhysicalInterface;
private:
	GD();
};

}

#endif
