/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "fs/bgl/ap/approach.h"
#include "fs/bgl/converter.h"
#include "fs/bgl/recordtypes.h"
#include "io/binarystream.h"
#include "fs/navdatabaseoptions.h"

#include <QDebug>

namespace atools {
namespace fs {
namespace bgl {

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
using Qt::hex;
using Qt::dec;
using Qt::endl;
#endif

using atools::io::BinaryStream;

Approach::Approach(const NavDatabaseOptions *options, BinaryStream *bs, rec::AirportRecordType airportRecType)
  : Record(options, bs)
{
  suffix = bs->readByte();
  runwayNumber = bs->readUByte();

  int typeFlags = bs->readUByte();
  type = static_cast<ap::ApproachType>(typeFlags & 0xf);
  runwayDesignator = (typeFlags >> 4) & 0x7;
  gpsOverlay = (typeFlags & 0x80) == 0x80;

  // TODO compare numbers with actual record occurence
  numTransitions = bs->readUByte();
  int numLegs = bs->readUByte();
  Q_UNUSED(numLegs)
  int numMissedLegs = bs->readUByte();
  Q_UNUSED(numMissedLegs)

  unsigned int fixFlags = bs->readUInt();
  fixType = static_cast<ap::fix::ApproachFixType>(fixFlags & 0xf);
  fixIdent = converter::intToIcao((fixFlags >> 5) & 0xfffffff, true);

  unsigned int fixIdentFlags = bs->readUInt();
  fixRegion = converter::intToIcao(fixIdentFlags & 0x7ff, true);
  fixAirportIdent = converter::intToIcao((fixIdentFlags >> 11) & 0x1fffff, true);

  altitude = bs->readFloat();
  heading = bs->readFloat(); // Heading is float degrees
  missedAltitude = bs->readFloat();

  if(airportRecType == rec::MSFS_APPROACH_NEW)
    bs->skip(4);

  // Read subrecords
  while(bs->tellg() < startOffset + size)
  {
    Record r(options, bs);
    rec::ApprRecordType recType = r.getId<rec::ApprRecordType>();
    if(checkSubRecord(r))
      return;

    switch(recType)
    {
      case rec::LEGS:
      case rec::LEGS_MSFS:
      case rec::LEGS_MSFS_116:
      case rec::LEGS_MSFS_118:
        if(options->isIncludedNavDbObject(type::APPROACHLEG))
        {
          int num = bs->readUShort();
          for(int i = 0; i < num; i++)
            legs.append(ApproachLeg(bs, recType));
        }
        break;

      case rec::MISSED_LEGS:
      case rec::MISSED_LEGS_MSFS:
      case rec::MISSED_LEGS_MSFS_116:
      case rec::MISSED_LEGS_MSFS_118:
        if(options->isIncludedNavDbObject(type::APPROACHLEG))
        {
          int num = bs->readUShort();
          for(int i = 0; i < num; i++)
            missedLegs.append(ApproachLeg(bs, recType));
        }
        break;

      case rec::TRANSITION:
      case rec::TRANSITION_MSFS:
      case rec::TRANSITION_MSFS_116:
        r.seekToStart();
        transitions.append(Transition(options, bs, recType));
        break;

      default:
#ifndef DEBUG_INFORMATION
        // Log unknown types only for other simulators than MSFS since this one comes up with  surprises
        if(opts->getSimulatorType() != atools::fs::FsPaths::SimulatorType::MSFS)
#endif
        qWarning().nospace().noquote() << Q_FUNC_INFO << " Unexpected record type 0x" << hex << recType << dec
                                       << " for airport ident " << fixAirportIdent << " offset " << bs->tellg();
    }
    r.seekToEnd();
  }
}

QDebug operator<<(QDebug out, const Approach& record)
{
  QDebugStateSaver saver(out);

  out.nospace().noquote() << static_cast<const Record&>(record)
                          << " Approach[type "
                          << ap::approachTypeToStr(record.type)
                          << ", rwy " << record.getRunwayName()
                          << ", gps overlay " << record.gpsOverlay
                          << ", fix type " << ap::approachFixTypeToStr(record.fixType)
                          << ", fix " << record.fixIdent
                          << ", fix region " << record.fixRegion
                          << ", ap icao " << record.fixAirportIdent
                          << ", alt " << record.altitude
                          << ", hdg " << record.heading << endl;
  out << record.transitions;
  out << record.legs;
  out << record.missedLegs;
  out << "]";
  return out;
}

Approach::~Approach()
{
}

QString Approach::getRunwayName() const
{
  return converter::runwayToStr(runwayNumber, runwayDesignator);
}

bool Approach::isValid() const
{
  bool valid = !legs.isEmpty();
  valid &= ap::approachTypeToStr(type) != "UNKN";
  for(const ApproachLeg& leg : legs)
    valid &= leg.isValid();
  for(const ApproachLeg& leg : missedLegs)
    valid &= leg.isValid();
  for(const Transition& trans: transitions)
    valid &= trans.isValid();
  return valid;

}

QString Approach::getDescription() const
{
  return QString("Approach[type ") + ap::approachTypeToStr(type) +
         ", rwy " + getRunwayName() +
         ", fix type " + ap::approachFixTypeToStr(fixType) +
         ", fix " + fixIdent +
         ", ap " + fixAirportIdent + "]";
}

} // namespace bgl
} // namespace fs
} // namespace atools
