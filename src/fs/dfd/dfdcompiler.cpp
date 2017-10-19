/*****************************************************************************
* Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
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

#include "fs/dfd/dfdcompiler.h"

#include "fs/common/metadatawriter.h"
#include "fs/common/magdecreader.h"
#include "settings/settings.h"
#include "sql/sqldatabase.h"
#include "sql/sqlutil.h"
#include "fs/progresshandler.h"
#include "fs/util/fsutil.h"
#include "fs/util/tacanfrequencies.h"
#include "sql/sqlscript.h"
#include "fs/navdatabaseoptions.h"
#include "fs/common/procedurewriter.h"
#include "geo/calculations.h"
#include "atools.h"
#include "fs/common/airportindex.h"

#include <QApplication>
#include <QDebug>
#include <QFileInfo>

using atools::fs::common::MagDecReader;
using atools::fs::common::MetadataWriter;
using atools::sql::SqlQuery;
using atools::sql::SqlUtil;
using atools::sql::SqlScript;
using atools::sql::SqlRecordVector;
using atools::sql::SqlRecord;
using atools::geo::Pos;
using atools::geo::DPos;
using atools::geo::Rect;
namespace utl = atools::fs::util;

namespace atools {
namespace fs {
namespace ng {

DfdCompiler::DfdCompiler(sql::SqlDatabase& sqlDb, const NavDatabaseOptions& opts,
                         ProgressHandler *progressHandler, NavDatabaseErrors *navdatabaseErrors)
  : options(opts), db(sqlDb), progress(progressHandler), errors(navdatabaseErrors)
{
  metadataWriter = new atools::fs::common::MetadataWriter(db);
  magDecReader = new atools::fs::common::MagDecReader();
  airportIndex = new atools::fs::common::AirportIndex();
  procWriter = new atools::fs::common::ProcedureWriter(db, airportIndex);
}

DfdCompiler::~DfdCompiler()
{
  close();
}

void DfdCompiler::writeAirports()
{
  progress->reportOther("Writing airports");

  airportRectMap.clear();
  longestRunwaySurfaceMap.clear();

  // Fill default values
  airportWriteQuery->bindValue(":fuel_flags", 0); // Not available
  airportWriteQuery->bindValue(":has_avgas", 0); // Not available
  airportWriteQuery->bindValue(":has_jetfuel", 0); // Not available
  airportWriteQuery->bindValue(":has_tower_object", 0); // Not available
  airportWriteQuery->bindValue(":is_closed", 0); // Not available
  airportWriteQuery->bindValue(":is_addon", 0); // Not available
  airportWriteQuery->bindValue(":num_boundary_fence", 0); // Not available
  airportWriteQuery->bindValue(":num_parking_gate", 0); // Not available
  airportWriteQuery->bindValue(":num_parking_ga_ramp", 0); // Not available
  airportWriteQuery->bindValue(":num_parking_cargo", 0); // Not available
  airportWriteQuery->bindValue(":num_parking_mil_cargo", 0); // Not available
  airportWriteQuery->bindValue(":num_parking_mil_combat", 0); // Not available
  airportWriteQuery->bindValue(":num_runway_light", 0); // Not available
  airportWriteQuery->bindValue(":num_runway_end_closed", 0); // Not available
  airportWriteQuery->bindValue(":num_runway_end_vasi", 0); // Not available
  airportWriteQuery->bindValue(":num_runway_end_als", 0); // Not available
  airportWriteQuery->bindValue(":num_apron", 0); // Not available
  airportWriteQuery->bindValue(":num_taxi_path", 0); // Not available
  airportWriteQuery->bindValue(":num_helipad", 0); // Not available
  airportWriteQuery->bindValue(":num_jetway", 0); // Not available
  airportWriteQuery->bindValue(":num_starts", 0); // Not available
  airportWriteQuery->bindValue(":rating", 1);

  airportWriteQuery->bindValue(":num_com", 0); // TODO fill later
  airportWriteQuery->bindValue(":num_approach", 0); // Filled later by procedure writer

  airportWriteQuery->bindValue(":num_runway_hard", 0); // Filled later
  airportWriteQuery->bindValue(":num_runway_soft", 0); // Filled later
  airportWriteQuery->bindValue(":num_runway_water", 0); // Filled later
  airportWriteQuery->bindValue(":longest_runway_length", 0); // Filled later
  airportWriteQuery->bindValue(":longest_runway_width", 0); // Filled later
  airportWriteQuery->bindValue(":longest_runway_heading", 0); // Filled later
  airportWriteQuery->bindValue(":num_runway_end_ils", 0); // Filled later
  airportWriteQuery->bindValue(":num_runways", 0); // Filled later

  airportQuery->exec();
  while(airportQuery->next())
  {
    Pos pos(airportQuery->valueFloat("airport_ref_longitude"),
            airportQuery->valueFloat("airport_ref_latitude"),
            airportQuery->valueFloat("elevation"));

    QString ident = airportQuery->valueStr("airport_identifier");

    Rect airportRect(pos);
    // Start with a minimum rectangle of about 100 meter
    airportRect.inflate(Pos::POS_EPSILON_100M, Pos::POS_EPSILON_100M);
    airportRectMap.insert(ident, airportRect);

    longestRunwaySurfaceMap.insert(ident, airportQuery->valueStr("longest_runway_surface_code"));

    airportWriteQuery->bindValue(":airport_id", ++curAirportId);
    airportIndex->addAirport(ident, curAirportId);

    airportWriteQuery->bindValue(":file_id", FILE_ID);
    airportWriteQuery->bindValue(":ident", ident);
    airportWriteQuery->bindValue(":name", utl::capAirportName(airportQuery->valueStr("airport_name")));
    airportWriteQuery->bindValue(":country", airportQuery->valueStr("area_code"));
    airportWriteQuery->bindValue(":is_military", utl::isNameMilitary(airportQuery->valueStr("airport_name")));

    airportWriteQuery->bindValue(":left_lonx", airportRect.getTopLeft().getLonX());
    airportWriteQuery->bindValue(":top_laty", airportRect.getTopLeft().getLatY());
    airportWriteQuery->bindValue(":right_lonx", airportRect.getBottomRight().getLonX());
    airportWriteQuery->bindValue(":bottom_laty", airportRect.getBottomRight().getLatY());

    airportWriteQuery->bindValue(":mag_var", magDecReader->getMagVar(pos));
    airportWriteQuery->bindValue(":altitude", pos.getAltitude());
    airportWriteQuery->bindValue(":lonx", pos.getLonX());
    airportWriteQuery->bindValue(":laty", pos.getLatY());
    airportWriteQuery->exec();
  }
  db.commit();
}

void DfdCompiler::writeRunways()
{
  progress->reportOther("Writing runways");

  runwayQuery->exec();

  SqlRecordVector runways;
  QString lastApt;
  while(runwayQuery->next())
  {
    QString apt = runwayQuery->valueStr("airport_identifier");

    if(!lastApt.isEmpty() && lastApt != apt)
      // Airport ID has changed write collected runways
      writeRunwaysForAirport(runways, lastApt);

    // Collect runways
    runways.append(runwayQuery->record());
    lastApt = apt;
  }
  writeRunwaysForAirport(runways, lastApt);
  db.commit();
}

void DfdCompiler::writeRunwaysForAirport(SqlRecordVector& runways, const QString& apt)
{
  QVector<std::pair<SqlRecord, SqlRecord> > runwaypairs;

  // area_code
  // icao_code
  // airport_identifier
  // runway_identifier
  // runway_latitude
  // runway_longitude
  // runway_gradient
  // runway_magnetic_bearing
  // runway_true_bearing
  // landing_threshold_elevation
  // displaced_threshold_distance
  // threshold_crossing_height
  // runway_length
  // runway_width
  // llz_identifier
  // llz_mls_gls_category

  // Find matching opposing ends
  pairRunways(runwaypairs, runways);

  int numRunways = 0, numRunwayIls = 0, longestRunwayLength = 0, longestRunwayWidth = 0;
  float longestRunwayHeading = 0.f;
  Rect airportRect = airportRectMap.value(apt);

  // Iterate over all runways / end pairs
  for(const std::pair<SqlRecord, SqlRecord>& runwaypair : runwaypairs)
  {
    SqlRecord primaryRec = runwaypair.first;
    SqlRecord secondaryRec = runwaypair.second;

    int primaryEndId = ++curRunwayEndId, secondaryEndId = ++curRunwayEndId;

    int length = primaryRec.valueInt("runway_length");
    int width = primaryRec.valueInt("runway_width");

    // Use average threshold altitude
    int alt = (primaryRec.valueInt("landing_threshold_elevation") +
               secondaryRec.valueInt("landing_threshold_elevation")) / 2;

    // Calculate center point
    float lonX = (primaryRec.valueFloat("runway_longitude") + secondaryRec.valueFloat("runway_longitude")) / 2.f;
    float latY = (primaryRec.valueFloat("runway_latitude") + secondaryRec.valueFloat("runway_latitude")) / 2.f;
    Pos pos(lonX, latY);

    // Calcuate true heading from magnetic which is needed for painting
    float magvar = magDecReader->getMagVar(pos);
    float heading = atools::geo::normalizeCourse(primaryRec.valueFloat("runway_magnetic_bearing") + magvar);
    float opposedHeading = atools::geo::normalizeCourse(secondaryRec.valueFloat("runway_magnetic_bearing") + magvar);

    // qDebug() << apt << primaryEndId << p.valueStr("runway_identifier")
    // << secondaryEndId << s.valueStr("runway_identifier");

    if(primaryRec.valueStr("llz_identifier").isEmpty())
      numRunwayIls++;

    // Remember the longest data
    if(length > longestRunwayLength)
    {
      longestRunwayLength = length;
      longestRunwayWidth = width;
      longestRunwayHeading = heading;
    }
    numRunways++;

    // Calculate the end coordinates
    float lengthMeter = atools::geo::feetToMeter(static_cast<float>(length));
    Pos primaryPos = pos.endpoint(lengthMeter / 2.f, opposedHeading).normalize();
    Pos secondaryPos = pos.endpoint(lengthMeter / 2.f, heading).normalize();
    airportRect.extend(primaryPos);
    airportRect.extend(secondaryPos);

    bool pClosed = primaryRec.valueBool("is_closed", false);
    bool sClosed = secondaryRec.valueBool("is_closed", false);

    // Write runway =======================================
    runwayWriteQuery->bindValue(":runway_id", ++curRunwayId);
    runwayWriteQuery->bindValue(":airport_id", airportIndex->getAirportId(apt));
    runwayWriteQuery->bindValue(":primary_end_id", primaryEndId);
    runwayWriteQuery->bindValue(":secondary_end_id", secondaryEndId);
    runwayWriteQuery->bindValue(":length", length);
    runwayWriteQuery->bindValue(":width", width);
    runwayWriteQuery->bindValue(":heading", heading);
    runwayWriteQuery->bindValue(":pattern_altitude", 0);
    runwayWriteQuery->bindValue(":marking_flags", 0);
    runwayWriteQuery->bindValue(":has_center_red", 0);
    runwayWriteQuery->bindValue(":primary_lonx", primaryPos.getLonX());
    runwayWriteQuery->bindValue(":primary_laty", primaryPos.getLatY());
    runwayWriteQuery->bindValue(":secondary_lonx", secondaryPos.getLonX());
    runwayWriteQuery->bindValue(":secondary_laty", secondaryPos.getLatY());
    runwayWriteQuery->bindValue(":altitude", alt);
    runwayWriteQuery->bindValue(":lonx", lonX);
    runwayWriteQuery->bindValue(":laty", latY);

    // Write the primary end =======================================
    runwayEndWriteQuery->bindValue(":runway_end_id", primaryEndId);
    runwayEndWriteQuery->bindValue(":name", primaryRec.valueStr("runway_identifier").mid(2));
    runwayEndWriteQuery->bindValue(":end_type", "P");
    runwayEndWriteQuery->bindValue(":offset_threshold", primaryRec.valueInt("displaced_threshold_distance"));
    runwayEndWriteQuery->bindValue(":blast_pad", 0);
    runwayEndWriteQuery->bindValue(":overrun", 0);
    runwayEndWriteQuery->bindValue(":has_closed_markings", 0);
    runwayEndWriteQuery->bindValue(":has_stol_markings", 0);
    runwayEndWriteQuery->bindValue(":is_takeoff", !pClosed);
    runwayEndWriteQuery->bindValue(":is_landing", !pClosed);
    runwayEndWriteQuery->bindValue(":is_pattern", 0);
    runwayEndWriteQuery->bindValue(":has_end_lights", 0);
    runwayEndWriteQuery->bindValue(":has_reils", 0);
    runwayEndWriteQuery->bindValue(":has_touchdown_lights", 0);
    runwayEndWriteQuery->bindValue(":num_strobes", 0);
    runwayEndWriteQuery->bindValue(":ils_ident", primaryRec.valueStr("llz_identifier"));
    runwayEndWriteQuery->bindValue(":heading", heading);
    runwayEndWriteQuery->bindValue(":lonx", primaryPos.getLonX());
    runwayEndWriteQuery->bindValue(":laty", primaryPos.getLatY());
    runwayEndWriteQuery->exec();

    // Write the secondary end =======================================
    runwayEndWriteQuery->bindValue(":runway_end_id", secondaryEndId);
    runwayEndWriteQuery->bindValue(":name", secondaryRec.valueStr("runway_identifier").mid(2));
    runwayEndWriteQuery->bindValue(":end_type", "S");
    runwayEndWriteQuery->bindValue(":offset_threshold", secondaryRec.valueInt("displaced_threshold_distance"));
    runwayEndWriteQuery->bindValue(":blast_pad", 0);
    runwayEndWriteQuery->bindValue(":overrun", 0);
    runwayEndWriteQuery->bindValue(":has_closed_markings", 0);
    runwayEndWriteQuery->bindValue(":has_stol_markings", 0);
    runwayEndWriteQuery->bindValue(":is_takeoff", !sClosed);
    runwayEndWriteQuery->bindValue(":is_landing", !sClosed);
    runwayEndWriteQuery->bindValue(":is_pattern", 0);
    runwayEndWriteQuery->bindValue(":has_end_lights", 0);
    runwayEndWriteQuery->bindValue(":has_reils", 0);
    runwayEndWriteQuery->bindValue(":has_touchdown_lights", 0);
    runwayEndWriteQuery->bindValue(":num_strobes", 0);
    runwayEndWriteQuery->bindValue(":ils_ident", secondaryRec.valueStr("llz_identifier"));
    runwayEndWriteQuery->bindValue(":heading", opposedHeading);
    runwayEndWriteQuery->bindValue(":lonx", secondaryPos.getLonX());
    runwayEndWriteQuery->bindValue(":laty", secondaryPos.getLatY());
    runwayEndWriteQuery->exec();

    runwayWriteQuery->exec();
  }

  runways.clear();

  // Do a workaround for insufficient runway information
  const QString& surface = longestRunwaySurfaceMap.value(apt);
  int numRunwayHard = 0, numRunwaySoft = 0, numRunwayWater = 0;
  if(surface == "H")
    // Assume all are hard if the longest is hard surface
    numRunwayHard = numRunways;
  else if(surface == "S")
  {
    // Assume all other runways are hard if longest is soft surface
    numRunwayHard = numRunways - 1;
    numRunwaySoft = 1;
  }
  else if(surface == "W")
  {
    // Assume all other runways are hard if longest is water surface
    numRunwayHard = numRunways - 1;
    numRunwayWater = 1;
  }

  // Update airport information
  airportUpdateQuery->bindValue(":aptid", airportIndex->getAirportId(apt));
  airportUpdateQuery->bindValue(":num_runway_hard", numRunwayHard);
  airportUpdateQuery->bindValue(":num_runway_soft", numRunwaySoft);
  airportUpdateQuery->bindValue(":num_runway_water", numRunwayWater);
  airportUpdateQuery->bindValue(":longest_runway_length", longestRunwayLength);
  airportUpdateQuery->bindValue(":longest_runway_width", longestRunwayWidth);
  airportUpdateQuery->bindValue(":longest_runway_heading", longestRunwayHeading);
  airportUpdateQuery->bindValue(":num_runway_end_ils", numRunwayIls);
  airportUpdateQuery->bindValue(":num_runways", numRunways);
  airportUpdateQuery->bindValue(":left_lonx", airportRect.getTopLeft().getLonX());
  airportUpdateQuery->bindValue(":top_laty", airportRect.getTopLeft().getLatY());
  airportUpdateQuery->bindValue(":right_lonx", airportRect.getBottomRight().getLonX());
  airportUpdateQuery->bindValue(":bottom_laty", airportRect.getBottomRight().getLatY());
  airportUpdateQuery->exec();
}

void DfdCompiler::pairRunways(QVector<std::pair<SqlRecord, SqlRecord> >& runwaypairs,
                              SqlRecordVector& runways)
{
  QSet<QString> found;
  for(const SqlRecord& rw : runways)
  {
    float heading = rw.valueFloat("runway_true_bearing");
    float opposedHeading = atools::geo::opposedCourseDeg(heading);
    QString rwident = rw.valueStr("runway_identifier");

    if(found.contains(rwident))
      // Already worked on that runway end
      continue;

    // RW11R -> 11R
    QString rname = rwident.mid(2);

    // 11
    int rnum = rname.mid(0, 2).toInt();

    // R
    QString desig = rname.size() > 2 ? rname.at(2) : QString();

    // Calculate opposed name
    QString opposedDesig = desig;
    if(opposedDesig == "R")
      opposedDesig = "L";
    else if(opposedDesig == "L")
      opposedDesig = "R";

    int opposedRnum = rnum + 18;
    if(opposedRnum > 36)
      opposedRnum -= 36;

    // RW29L
    QString opposedRname = "RW" + (opposedRnum < 10 ? "0" : QString()) + QString::number(opposedRnum) + opposedDesig;

    // Try to find the other end in the list
    bool foundEnd = false;
    for(const SqlRecord& orw : runways)
    {
      if(orw.valueStr("runway_identifier") == opposedRname)
      {
        // Remember that we already worked on this
        found.insert(opposedRname);
        found.insert(rwident);

        runwaypairs.append(std::make_pair(rw, orw));
        foundEnd = true;
        break;
      }
    }

    if(!foundEnd)
    {
      // Assume other end is closed if no pair way found
      SqlRecord orec(rw);
      orec.setValue("runway_identifier", opposedRname);
      orec.setValue("displaced_threshold_distance", 0);
      orec.setValue("llz_identifier", QVariant(QVariant::String));
      orec.setValue("runway_true_bearing", opposedHeading);

      orec.appendField("is_closed", QVariant::Bool);
      orec.setValue("is_closed", true);

      runwaypairs.append(std::make_pair(rw, orec));
    }
  }
}

void DfdCompiler::writeNavaids()
{
  progress->reportOther("Writing navaids");

  SqlScript script(db, true /*options->isVerbose()*/);

  // Write VOR and NDB
  script.executeScript(":/atools/resources/sql/fs/db/airac/populate_navaids.sql");
  db.commit();
}

void DfdCompiler::writeAirways()
{
  progress->reportOther("Writing airways");

  // Get airways joined with waypoints
  QString query(
    "select  a.route_identifier, a.seqno, a.flightlevel, a.waypoint_description_code, w.waypoint_id, "
    "  a.direction_restriction, a.minimum_altitude1, a.minimum_altitude2, a.maximum_altitude, "
    "  w.lonx, w.laty "
    "from src.tbl_airways_pr a "
    "join waypoint w on "
    "  a.waypoint_identifier = w.ident and a.icao_code = w.region and a.waypoint_longitude = w.lonx and "
    "  a.waypoint_latitude = w.laty "
    "order by route_identifier, seqno");
  SqlQuery airways(query, db);

  // Insert into airway and let SQLite autogenerate an ID
  SqlQuery insert(db);
  insert.prepare(SqlUtil(db).buildInsertStatement("airway", QString(), {"airway_id"}));

  SqlRecord lastRec;
  QString lastName;
  bool lastEndOfRoute = true;
  airways.exec();
  int sequenceNumber = 1, fragmentNumber = 1;
  while(airways.next())
  {
    QString name = airways.valueStr("route_identifier");
    QString code = airways.valueStr("waypoint_description_code");
    bool nameChange = !lastName.isEmpty() && name != lastName;

    if(!lastName.isEmpty())
    {
      // Not the first iteration

      if(!nameChange && lastEndOfRoute)
      {
        // No name change but the last row indicated end of route - new fragment
        fragmentNumber++;
        sequenceNumber = 1;
      }

      if(!lastEndOfRoute && !nameChange)
      {
        // Nothing has changed or ended - insert from/to pair

        Pos fromPos(lastRec.valueFloat("lonx"), lastRec.valueFloat("laty"));
        Pos toPos(airways.valueFloat("lonx"), airways.valueFloat("laty"));
        Rect rect(fromPos);
        rect.extend(toPos);

        insert.bindValue(":airway_name", lastRec.valueStr("route_identifier"));

        // -- V = victor, J = jet, B = both
        // B = All Altitudes, H = High Level Airways, L = Low Level Airways
        QString awType = lastRec.valueStr("flightlevel");
        if(awType == "H")
          insert.bindValue(":airway_type", "J");
        else if(awType == "L")
          insert.bindValue(":airway_type", "V");
        else
          insert.bindValue(":airway_type", "B");

        insert.bindValue(":airway_fragment_no", fragmentNumber);
        insert.bindValue(":sequence_no", sequenceNumber++);
        insert.bindValue(":from_waypoint_id", lastRec.valueInt("waypoint_id"));
        insert.bindValue(":to_waypoint_id", airways.valueInt("waypoint_id"));

        // -- N = none, B = backward, F = forward
        // F =  One way in direction route is coded (Forward),
        // B = One way in opposite direction route is coded (backwards)
        // blank = // no restrictions on direction
        QString dir = lastRec.valueStr("direction_restriction");
        if(dir.isEmpty() || dir == " ")
          dir = "N";

        insert.bindValue(":direction", dir);

        insert.bindValue(":minimum_altitude", lastRec.valueInt("minimum_altitude1"));
        insert.bindValue(":maximum_altitude", lastRec.valueInt("maximum_altitude"));

        insert.bindValue(":left_lonx", rect.getTopLeft().getLonX());
        insert.bindValue(":top_laty", rect.getTopLeft().getLatY());
        insert.bindValue(":right_lonx", rect.getBottomRight().getLonX());
        insert.bindValue(":bottom_laty", rect.getBottomRight().getLatY());

        insert.bindValue(":from_lonx", fromPos.getLonX());
        insert.bindValue(":from_laty", fromPos.getLatY());
        insert.bindValue(":to_lonx", toPos.getLonX());
        insert.bindValue(":to_laty", toPos.getLatY());
        insert.exec();
      }
    }

    lastRec = airways.record();
    lastName = name;
    lastEndOfRoute = code.size() > 1 && code.at(1) == "E";

    if(nameChange)
    {
      // Name has changed - reset all
      fragmentNumber = 1;
      sequenceNumber = 1;
    }
  }
  db.commit();
}

void DfdCompiler::writeProcedures()
{
  progress->reportOther("Writing approaches and transitions");
  writeProcedure("src.tbl_iaps_pr", "APPCH");

  progress->reportOther("Writing SIDs");
  writeProcedure("src.tbl_sids_pr", "SID");

  progress->reportOther("Writing STARs");
  writeProcedure("src.tbl_stars_pr", "STAR");
}

void DfdCompiler::writeProcedure(const QString& table, const QString& rowCode)
{
  // Get procedures ordered from the table
  SqlQuery query(SqlUtil(db).buildSelectStatement(table) +
                 // " where airport_identifier in ('CYBK') "
                 // "and procedure_identifier = 'R34'"
                 " order by airport_identifier, procedure_identifier, route_type, transition_identifier, seqno ", db);
  query.exec();
  atools::fs::common::ProcedureInput procInput;

  QString curAirport;
  procInput.rowCode = rowCode;
  int num = 0;
  while(query.next())
  {
    QString airportIdent = query.valueStr("airport_identifier");

    // Give some feedback for long process
    if((++num % 10000) == 0)
      qDebug() << num << airportIdent << "...";

    if(!curAirport.isEmpty() && airportIdent != curAirport)
    {
      procWriter->finish(procInput);
      procWriter->reset();
    }

    // qDebug() << query.record();
    // Fill context for error reporting
    procInput.context = QString("File %1, airport %2, procedure %3, transition %4").
                        arg(db.databaseName()).
                        arg(query.valueStr("airport_identifier")).
                        arg(query.valueStr("procedure_identifier")).
                        arg(query.valueStr("transition_identifier"));

    procInput.airportIdent = airportIdent;
    procInput.airportId = airportIndex->getAirportId(airportIdent).toInt();

    fillProcedureInput(procInput, query);

    // Leave the complicated states to the procedure writer
    procWriter->write(procInput);

    curAirport = procInput.airportIdent;
  }
  procWriter->finish(procInput);
  procWriter->reset();
}

void DfdCompiler::fillProcedureInput(atools::fs::common::ProcedureInput& procInput, const atools::sql::SqlQuery& query)
{
  procInput.seqNr = query.valueInt("seqno");
  procInput.routeType = atools::strToChar(query.valueStr("route_type"));
  procInput.sidStarAppIdent = query.valueStr("procedure_identifier");
  procInput.transIdent = query.valueStr("transition_identifier");
  procInput.fixIdent = query.valueStr("waypoint_identifier");
  procInput.icaoCode = query.valueStr("waypoint_icao_code");
  // procInput.secCode = query.valueStr(""); // Not available
  // procInput.subCode = query.valueStr(""); // Not available
  procInput.descCode = query.valueStr("waypoint_description_code");
  procInput.waypointPos = DPos(query.valueDouble("waypoint_longitude"), query.valueDouble("waypoint_latitude"));

  procInput.turnDir = query.valueStr("turn_direction");
  procInput.pathTerm = query.valueStr("path_termination");
  procInput.recdNavaid = query.valueStr("recommanded_navaid");
  // procInput.recdIcaoCode = query.valueStr(""); // Not available
  // procInput.recdSecCode = query.valueStr("");  // Not available
  // procInput.recdSubCode = query.valueStr("");  // Not available
  procInput.recdWaypointPos = DPos(query.valueDouble("recommanded_navaid_longitude"),
                                   query.valueDouble("recommanded_navaid_latitude"));

  procInput.theta = query.valueFloat("theta");
  procInput.rho = query.valueFloat("rho");
  procInput.magCourse = query.valueFloat("magnetic_course");

  procInput.rteHoldTime = procInput.rteHoldDist = 0.f;
  if(procInput.pathTerm.startsWith("H"))
    procInput.rteHoldTime = query.valueFloat("route_distance_holding_distance_time");
  else
    procInput.rteHoldDist = query.valueFloat("route_distance_holding_distance_time");

  procInput.altDescr = query.valueStr("altitude_description");
  procInput.altitude = query.valueStr("altitude1");
  procInput.altitude2 = query.valueStr("altitude2");
  procInput.transAlt = query.valueStr("transition_altitude");
  procInput.speedLimitDescr = query.valueStr("speed_limit_description");
  procInput.speedLimit = query.valueInt("speed_limit");

  procInput.centerFixOrTaaPt = query.valueStr("center_waypoint");
  // procInput.centerIcaoCode = query.valueStr(""); // Not available
  // procInput.centerSecCode = query.valueStr("");  // Not available
  // procInput.centerSubCode = query.valueStr("");  // Not available
  procInput.centerPos = DPos(query.valueDouble("center_waypoint_longitude"),
                             query.valueDouble("center_waypoint_latitude"));

  // procInput.gnssFmsIndicator = query.valueStr("");
}

void DfdCompiler::close()
{
  delete magDecReader;
  magDecReader = nullptr;

  delete metadataWriter;
  metadataWriter = nullptr;

  deInitQueries();

  delete procWriter;
  procWriter = nullptr;

  delete airportIndex;
  airportIndex = nullptr;
}

void DfdCompiler::readHeader()
{
  // Extract cycle
  metadataQuery->exec();
  if(metadataQuery->next())
    airacCycle = metadataQuery->valueStr("current_airac");
}

void DfdCompiler::compileMagDeclBgl()
{
  // Look first in config dir and then in local dir
  QString file =
    atools::settings::Settings::instance().getOverloadedPath(atools::buildPath({QApplication::applicationDirPath(),
                                                                                "magdec", "magdec.bgl"}));

  qInfo() << "Reading" << file;

  magDecReader->readFromBgl(file);
  magDecReader->writeToTable(db);
  db.commit();
}

void DfdCompiler::writeFileAndSceneryMetadata()
{
  metadataWriter->writeSceneryArea(QString(), "Navigraph", SCENERY_ID);
  metadataWriter->writeFile(QString(), QString(), SCENERY_ID, FILE_ID);
  db.commit();
}

void DfdCompiler::updateMagvar()
{
  progress->reportOther("Updating magnetic declination");

  atools::fs::common::MagDecReader *magdec = magDecReader;
  SqlUtil::UpdateColFuncType func =
    [magdec](const atools::sql::SqlQuery& from, atools::sql::SqlQuery& to) -> bool
    {
      to.bindValue(":mag_var", magdec->getMagVar(Pos(from.valueFloat("lonx"),
                                                     from.valueFloat("laty"))));
      return true;
    };

  SqlUtil util(db);
  util.updateColumnInTable("waypoint", "waypoint_id", {"lonx", "laty"}, {"mag_var"}, func);
  util.updateColumnInTable("ndb", "ndb_id", {"lonx", "laty"}, {"mag_var"}, func);
  db.commit();
}

void DfdCompiler::updateTacanChannel()
{
  progress->reportOther("Updating VORTAC and TACAN channels");

  SqlUtil::UpdateColFuncType func =
    [](const atools::sql::SqlQuery& from, atools::sql::SqlQuery& to) -> bool
    {
      QString type = from.valueStr("type");
      if(type == "TC" || type.startsWith("VT")) // TACAN or VORTAC
      {
        to.bindValue(":channel", atools::fs::util::tacanChannelForFrequency(from.valueInt("frequency") / 10));
        return true;
      }
      else
        return false;
    };
  SqlUtil(db).updateColumnInTable("vor", "vor_id", {"frequency", "type"}, {"channel"}, func);
  db.commit();
}

void DfdCompiler::updateIlsGeometry()
{
  progress->reportOther("Updating ILS geometry");

  int featherLen = ILS_FEATHER_LEN_NM;
  SqlUtil::UpdateColFuncType func =
    [featherLen](const atools::sql::SqlQuery& from, atools::sql::SqlQuery& to) -> bool
    {
      // Position of the pointy end
      Pos pos(from.valueFloat("lonx"), from.valueFloat("laty"));

      float length = atools::geo::nmToMeter(featherLen);
      float width = from.valueFloat("loc_width");
      float heading = atools::geo::opposedCourseDeg(from.valueFloat("loc_heading"));

      // Corner endpoints
      Pos p1 = pos.endpoint(length, heading - width / 2.f).normalize();
      Pos p2 = pos.endpoint(length, heading + width / 2.f).normalize();

      // Calculated the center point between corners - move it a bit towareds the pointy end
      float featherWidth = p1.distanceMeterTo(p2);
      Pos pmid = pos.endpoint(length - featherWidth / 2, heading).normalize();

      to.bindValue(":end1_lonx", p1.getLonX());
      to.bindValue(":end1_laty", p1.getLatY());
      to.bindValue(":end_mid_lonx", pmid.getLonX());
      to.bindValue(":end_mid_laty", pmid.getLatY());
      to.bindValue(":end2_lonx", p2.getLonX());
      to.bindValue(":end2_laty", p2.getLatY());
      return true;
    };

  SqlUtil(db).updateColumnInTable("ils", "ils_id", {"lonx", "laty", "loc_heading", "loc_width"},
                                  {"end1_lonx", "end1_laty", "end_mid_lonx", "end_mid_laty", "end2_lonx", "end2_laty"},
                                  func);
  db.commit();
}

void DfdCompiler::initQueries()
{
  deInitQueries();

  if(metadataWriter != nullptr)
    metadataWriter->initQueries();

  airportQuery = new SqlQuery(db);
  airportQuery->prepare("select * from src.tbl_airports_pr order by airport_identifier");

  airportWriteQuery = new SqlQuery(db);
  airportWriteQuery->prepare(
    SqlUtil(db).buildInsertStatement("airport", QString(), {
          "tower_frequency", "atis_frequency", "awos_frequency", "asos_frequency", "unicom_frequency",
          "city", "state",
          "largest_parking_ramp", "largest_parking_gate",
          "scenery_local_path", "bgl_filename",
          "longest_runway_surface",
          "tower_altitude", "tower_lonx", "tower_laty"
        }, true /* named bindings */));

  runwayQuery = new SqlQuery(db);
  runwayQuery->prepare("select * from src.tbl_runways_pr order by icao_code, airport_identifier, runway_identifier");

  runwayWriteQuery = new SqlQuery(db);
  runwayWriteQuery->prepare(
    SqlUtil(db).buildInsertStatement("runway", QString(),
                                     {"surface", "shoulder", "edge_light", "center_light"}));

  runwayEndWriteQuery = new SqlQuery(db);
  runwayEndWriteQuery->prepare(SqlUtil(db).buildInsertStatement("runway_end", QString(), {
          "left_vasi_type", "left_vasi_pitch", "right_vasi_type", "right_vasi_pitch", "app_light_system_type"
        }));

  airportUpdateQuery = new SqlQuery(db);
  airportUpdateQuery->prepare("update airport set "
                              "num_runway_hard = :num_runway_hard, "
                              "num_runway_soft = :num_runway_soft, "
                              "num_runway_water = :num_runway_water, "
                              "longest_runway_length = :longest_runway_length, "
                              "longest_runway_width = :longest_runway_width, "
                              "longest_runway_heading = :longest_runway_heading, "
                              "num_runway_end_ils = :num_runway_end_ils, "
                              "num_runways = :num_runways, "
                              "left_lonx = :left_lonx, "
                              "top_laty = :top_laty, "
                              "right_lonx = :right_lonx, "
                              "bottom_laty = :bottom_laty where airport_id = :aptid");

  metadataQuery = new SqlQuery(db);
  metadataQuery->prepare(SqlUtil(db).buildSelectStatement("src.tbl_header"));
}

void DfdCompiler::deInitQueries()
{
  if(metadataWriter != nullptr)
    metadataWriter->deInitQueries();

  delete airportQuery;
  airportQuery = nullptr;

  delete runwayQuery;
  runwayQuery = nullptr;

  delete runwayWriteQuery;
  runwayWriteQuery = nullptr;

  delete runwayEndWriteQuery;
  runwayEndWriteQuery = nullptr;

  delete airportWriteQuery;
  airportWriteQuery = nullptr;

  delete airportUpdateQuery;
  airportUpdateQuery = nullptr;

  delete metadataQuery;
  metadataQuery = nullptr;
}

void DfdCompiler::attachDatabase()
{
  db.attachDatabase(options.getSourceDatabase(), "src");
}

void DfdCompiler::detachDatabase()
{
  db.detachDatabase("src");
}

} // namespace ng
} // namespace fs
} // namespace atools
