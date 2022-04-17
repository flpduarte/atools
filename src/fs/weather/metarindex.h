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

#ifndef ATOOLS_METARINDEX_H
#define ATOOLS_METARINDEX_H

#include "fs/weather/weathertypes.h"

class QTextStream;

namespace atools {

namespace geo {
template<typename TYPE>
class SpatialIndex;
}
namespace fs {
namespace weather {

struct MetarResult;
struct MetarData;

/*
 * Reads, caches and indexes (by position) METAR reports in NOAA style as also used by X-Plane.
 * Can also read flat, plain text METAR files like they are provided by IVAO or VATSIM.
 *
 * Example for XPLANE or NOAA:
 *
 * 2017/07/30 18:45
 * KHYI 301845Z 13007KT 070V130 10SM SCT075 38/17 A2996
 *
 * 2017/07/30 18:55
 * KPRO 301855Z AUTO 11003KT 10SM CLR 26/14 A3022 RMK AO2 T02570135
 *
 * 2017/07/30 18:47
 * KADS 301847Z 06005G14KT 13SM SKC 32/19 A3007
 *
 * Example for format FLAT:
 *
 * KC99 100906Z AUTO 30022G42KT 10SM CLR M01/M04 A3035 RMK AO2
 * LCEN 100920Z 16004KT 090V230 CAVOK 31/10 Q1010 NOSIG
 */
class MetarIndex
{
public:
  MetarIndex(atools::fs::weather::MetarFormat formatParam, bool verboseLogging = false);
  ~MetarIndex();

  MetarIndex(const MetarIndex& other) = delete;
  MetarIndex& operator=(const MetarIndex& other) = delete;

  /* Read METARs from stream and add them to the index. Merges into current list or clears list before.
   * Older of duplicates are ignored/removed.
   * Returns number of METARs read. */
  int read(QTextStream& stream, const QString& fileName, bool merge);

  /* Clears all lists */
  void clear();

  /* true if nothing was read */
  bool isEmpty() const;

  /* Number of unique airport idents in index */
  int size() const;

  /* Get METAR information for station or nearest.
   * Also keeps position and ident of original request.*/
  atools::fs::weather::MetarResult getMetar(const QString& station, const atools::geo::Pos& pos);

  /* Set to a function that returns the coordinates for an airport ident. Needed to find the nearest. */
  void setFetchAirportCoords(const std::function<atools::geo::Pos(const QString&)>& value)
  {
    fetchAirportCoords = value;
  }

private:
  /* Get a METAR string. Empty if not available */
  MetarData metarData(const QString& ident);

  /* Read NOAA or XPLANE format */
  int readNoaaXplane(QTextStream& stream, const QString& fileOrUrl, bool merge);

  /* Read flat file format like VATSIM */
  int readFlat(QTextStream& stream, const QString& fileOrUrl, bool merge);

  /* Read JSON file format from IVAO */
  int readJson(QTextStream& stream, const QString& fileOrUrl, bool merge);

  /* Copy airports from the complete list to the index with coordinates.
   * Copies only airports that exist in the current simulator database, i.e. where fetchAirportCoords returns
   * a valid coordinate. */
  void updateIndex();

  /* Update or insert a METAR entry */
  void updateOrInsert(const QString& metar, const QString& ident, const QDateTime& lastTimestamp);

  /* Callback to get airport coodinates by ICAO ident */
  std::function<atools::geo::Pos(const QString&)> fetchAirportCoords;

  /* Map containing all found METARs airport idents mapped to the position in the spatial index */
  QHash<QString, int> identIndexMap;

  /* Index containing all stations. Stations without valid position will be located at x/y/z = 0/0/0 and therfore
   * not considered in the index. */
  atools::geo::SpatialIndex<MetarData> *spatialIndex = nullptr;

  bool verbose = false;
  atools::fs::weather::MetarFormat format = atools::fs::weather::UNKNOWN;

};

} // namespace weather
} // namespace fs
} // namespace atools

#endif // ATOOLS_METARINDEX_H
