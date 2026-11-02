// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include <limits>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Cylinder.hxx>
#include <BRep_Builder.hxx>
#include <Mod/Part/App/FCBRepAlgoAPI_Cut.h>
#include <Mod/Part/App/FCBRepAlgoAPI_Fuse.h>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_Circle.hxx>
#include <Standard_Version.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp.hxx>

#include <App/Application.h>
#include <App/DocumentObject.h>
#include <Base/Placement.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Mod/Part/App/FaceMakerCheese.h>
#include <Mod/Part/App/TopoShapeMapper.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/Tools.h>

#include "FeatureHole.h"
#include "nlohmann/json.hpp"

#include <numbers>

FC_LOG_LEVEL_INIT("PartDesign", true, true);

namespace PartDesign
{

/* TRANSLATOR PartDesign::Hole */

const char* Hole::DepthTypeEnums[] = {"Dimension", "ThroughAll", /*, "UpToFirst", */ nullptr};
const char* Hole::ThreadDepthTypeEnums[] = {"Hole Depth", "Dimension", "Tapped (DIN76)", nullptr};

const char* Hole::ClearanceNoneEnums[] = {"-", "-", "-", nullptr};
const char* Hole::ClearanceMetricEnums[] = {"Medium", "Fine", "Coarse", nullptr};
const char* Hole::ClearanceUTSEnums[] = {"Normal", "Close", "Loose", nullptr};
const char* Hole::ClearanceOtherEnums[] = {"Normal", "Close", "Wide", nullptr};
const char* Hole::DrillPointEnums[] = {"Flat", "Angled", nullptr};

/* "None" profile */

const char* Hole::HoleCutType_None_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};

const double Hole::metricHoleDiameters[51][4] = {
    /* ISO metric clearance hole diameters according to ISO 273 */
    // {screw diameter, fine, medium, coarse}
    {1.0, 1.1, 1.2, 1.3},
    {1.2, 1.3, 1.4, 1.5},
    {1.4, 1.5, 1.6, 1.8},
    {1.6, 1.7, 1.8, 2.0},
    {1.8, 2.0, 2.1, 2.2},
    {2.0, 2.2, 2.4, 2.6},
    {2.5, 2.7, 2.9, 3.1},
    {3.0, 3.2, 3.4, 3.6},
    {3.5, 3.7, 3.9, 4.2},
    {4.0, 4.3, 4.5, 4.8},
    {4.5, 4.8, 5.0, 5.3},
    {5.0, 5.3, 5.5, 5.8},
    {6.0, 6.4, 6.6, 7.0},
    {7.0, 7.4, 7.6, 8.0},
    {8.0, 8.4, 9.0, 10.0},
    // 9.0 undefined
    {10.0, 10.5, 11.0, 12.0},
    // 11.0 undefined
    {12.0, 13.0, 13.5, 14.5},
    {14.0, 15.0, 15.5, 16.5},
    {16.0, 17.0, 17.5, 18.5},
    {18.0, 19.0, 20.0, 21.0},
    {20.0, 21.0, 22.0, 24.0},
    {22.0, 23.0, 24.0, 26.0},
    {24.0, 25.0, 26.0, 28.0},
    {27.0, 28.0, 30.0, 32.0},
    {30.0, 31.0, 33.0, 35.0},
    {33.0, 34.0, 36.0, 38.0},
    {36.0, 37.0, 39.0, 42.0},
    {39.0, 40.0, 42.0, 45.0},
    {42.0, 43.0, 45.0, 48.0},
    {45.0, 46.0, 48.0, 52.0},
    {48.0, 50.0, 52.0, 56.0},
    {52.0, 54.0, 56.0, 62.0},
    {56.0, 58.0, 62.0, 66.0},
    {60.0, 62.0, 66.0, 70.0},
    {64.0, 66.0, 70.0, 74.0},
    {68.0, 70.0, 74.0, 78.0},
    {72.0, 74.0, 78.0, 82.0},
    {76.0, 78.0, 82.0, 86.0},
    {80.0, 82.0, 86.0, 91.0},
    {85.0, 87.0, 91.0, 96.0},
    {90.0, 93.0, 96.0, 101.0},
    {95.0, 98.0, 101.0, 107.0},
    {100.0, 104.0, 107.0, 112.0},
    {105.0, 109.0, 112.0, 117.0},
    {110.0, 114.0, 117.0, 122.0},
    {115.0, 119.0, 122.0, 127.0},
    {120.0, 124.0, 127.0, 132.0},
    {125.0, 129.0, 132.0, 137.0},
    {130.0, 134.0, 137.0, 144.0},
    {140.0, 144.0, 147.0, 155.0},
    {150.0, 155.0, 158.0, 165.0}
};

const Hole::UTSClearanceDefinition Hole::UTSHoleDiameters[23] = {
    /* UTS clearance hole diameters according to ASME B18.2.8 */
    // for information: the norm defines a drill bit number (that is in turn standardized in another
    // ASME norm). as result the norm defines a minimal clearance which is the diameter of that
    // drill bit. we use here this minimal clearance as the theoretical exact hole diameter as this
    // is also done in the ISO norm. {screw class, close, normal, loose}
    {"#0", 1.7, 1.9, 2.4},
    {"#1", 2.1, 2.3, 2.6},
    {"#2", 2.4, 2.6, 2.9},
    {"#3", 2.7, 2.9, 3.3},
    {"#4", 3.0, 3.3, 3.7},
    {"#5", 3.6, 4.0, 4.4},
    {"#6", 3.9, 4.3, 4.7},
    {"#8", 4.6, 5.0, 5.4},
    {"#10", 5.2, 5.6, 6.0},
    // "#12" not defined
    {"1/4", 6.8, 7.1, 7.5},
    {"5/16", 8.3, 8.7, 9.1},
    {"3/8", 9.9, 10.3, 10.7},
    {"7/16", 11.5, 11.9, 12.3},
    {"1/2", 13.5, 14.3, 15.5},
    // "9/16" not defined
    {"5/8", 16.7, 17.5, 18.6},
    {"3/4", 19.8, 20.6, 23.0},
    {"7/8", 23.0, 23.8, 26.2},
    {"1", 26.2, 27.8, 29.4},
    {"1 1/8", 29.4, 31.0, 33.3},
    {"1 3/16", 31.0, 32.5, 34.9},
    {"1 1/4", 32.5, 34.1, 36.5},
    {"1 3/8", 36.5, 38.1, 40.9},
    {"1 1/2", 39.7, 41.3, 44.0}
};

std::vector<std::string> getThreadDesignations(const ThreadUtils& threadUtils, const int threadType)
{
    if (threadType <= 0) {
        return {"---"};
    }
    const auto& definitions = threadUtils.getThreadDefinitions();
    const size_t defIndex = static_cast<size_t>(threadType) - 1;
    if (defIndex >= definitions.size()) {
        return {};
    }
    return definitions[defIndex].designations;
}

/* ISO coarse metric enums */
std::vector<std::string> Hole::HoleCutType_ISOmetric_Enums
    = {"None", "Counterbore", "Countersink", "Counterdrill"};
const char* Hole::ThreadClass_ISOmetric_Enums[]
    = {"4G", "4H", "5G", "5H", "6G", "6H", "7G", "7H", "8G", "8H", nullptr};

std::vector<std::string> Hole::HoleCutType_ISOmetricfine_Enums
    = {"None", "Counterbore", "Countersink", "Counterdrill"};
const char* Hole::ThreadClass_ISOmetricfine_Enums[]
    = {"4G", "4H", "5G", "5H", "6G", "6H", "7G", "7H", "8G", "8H", nullptr};

/* Details from https://en.wikipedia.org/wiki/Unified_Thread_Standard */

/* UTS coarse */
const char* Hole::HoleCutType_UNC_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNC_Enums[] = {"1B", "2B", "3B", nullptr};

/* UTS fine */
const char* Hole::HoleCutType_UNF_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNF_Enums[] = {"1B", "2B", "3B", nullptr};

/* UTS extrafine */
const char* Hole::HoleCutType_UNEF_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_UNEF_Enums[] = {"1B", "2B", "3B", nullptr};

/* NPT */
const char* Hole::HoleCutType_NPT_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};

/* BSP */
const char* Hole::HoleCutType_BSP_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};

/* BSW */
const char* Hole::HoleCutType_BSW_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_BSW_Enums[] = {"Medium", "Normal", nullptr};

/* BSF */
const char* Hole::HoleCutType_BSF_Enums[]
    = {"None", "Counterbore", "Countersink", "Counterdrill", nullptr};
const char* Hole::ThreadClass_BSF_Enums[] = {"Medium", "Normal", nullptr};

PROPERTY_SOURCE(PartDesign::Hole, PartDesign::ProfileBased)

const App::PropertyAngle::Constraints Hole::floatAngle = {
    Base::toDegrees<double>(Precision::Angular()),
    180.0 - Base::toDegrees<double>(Precision::Angular()),
    1.0
};
// OCC can only create holes with a min diameter of 10 times the Precision::Confusion()
const App::PropertyQuantityConstraint::Constraints diameterRange
    = {10 * Precision::Confusion(), std::numeric_limits<float>::max(), 1.0};

// Custom clearance can be negative or positive to adjust for manufacturing
const App::PropertyQuantityConstraint::Constraints clearanceRange
    = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 0.1};

Hole::Hole()
{
    addSubType = FeatureAddSub::Subtractive;

    threadUtils.executeReadThreadDefinitions();

    readCutDefinitions();

    ADD_PROPERTY_TYPE(Threaded, (false), "Hole", App::Prop_None, "Threaded");

    ADD_PROPERTY_TYPE(ModelThread, (false), "Hole", App::Prop_None, "Model actual thread");

    ADD_PROPERTY_TYPE(CosmeticThread, (true), "Hole", App::Prop_None, "Texture the thread");

    ADD_PROPERTY_TYPE(ThreadType, (0L), "Hole", App::Prop_None, "Thread type");
    ThreadType.setEnums(ThreadUtils::ThreadTypeEnums);

    ADD_PROPERTY_TYPE(ThreadSize, (0L), "Hole", App::Prop_None, "Thread size");
    ThreadSize.setEnums(getThreadDesignations(threadUtils, ThreadType.getValue()));

    ADD_PROPERTY_TYPE(ThreadClass, (0L), "Hole", App::Prop_None, "Thread class");
    ThreadClass.setEnums(ThreadUtils::ThreadClass_None_Enums);

    ADD_PROPERTY_TYPE(ThreadFit, (0L), "Hole", App::Prop_None, "Clearance hole fit");
    ThreadFit.setEnums(ClearanceMetricEnums);

    ADD_PROPERTY_TYPE(Diameter, (6.0), "Hole", App::Prop_None, "Diameter");
    Diameter.setConstraints(&diameterRange);

    ADD_PROPERTY_TYPE(ThreadDiameter, (0.0), "Hole", App::Prop_None, "Thread major diameter");
    ThreadDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(ThreadDirection, (0L), "Hole", App::Prop_None, "Thread direction");
    ThreadDirection.setEnums(ThreadUtils::ThreadDirectionEnums);
    ThreadDirection.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutType, (0L), "Hole", App::Prop_None, "Head cut type");
    HoleCutType.setEnums(HoleCutType_None_Enums);

    ADD_PROPERTY_TYPE(HoleCutCustomValues, (false), "Hole", App::Prop_None, "Custom cut values");
    HoleCutCustomValues.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDiameter, (0.0), "Hole", App::Prop_None, "Head cut diameter");
    HoleCutDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDepth, (0.0), "Hole", App::Prop_None, "Head cut depth");
    HoleCutDepth.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutCountersinkAngle, (90.0), "Hole", App::Prop_None, "Head cut countersink angle");
    HoleCutCountersinkAngle.setConstraints(&floatAngle);
    HoleCutCountersinkAngle.setReadOnly(true);

    ADD_PROPERTY_TYPE(DepthType, (0L), "Hole", App::Prop_None, "Type");
    DepthType.setEnums(DepthTypeEnums);

    ADD_PROPERTY_TYPE(Depth, (25.0), "Hole", App::Prop_None, "Length");

    ADD_PROPERTY_TYPE(DrillPoint, (1L), "Hole", App::Prop_None, "Drill point type");
    DrillPoint.setEnums(DrillPointEnums);

    ADD_PROPERTY_TYPE(DrillPointAngle, (118.0), "Hole", App::Prop_None, "Drill point angle");
    DrillPointAngle.setConstraints(&floatAngle);
    ADD_PROPERTY_TYPE(
        DrillForDepth,
        ((long)0),
        "Hole",
        App::Prop_None,
        "The size of the drill point will be taken into\n account for the depth of blind holes"
    );

    ADD_PROPERTY_TYPE(Tapered, (false), "Hole", App::Prop_None, "Tapered");

    ADD_PROPERTY_TYPE(TaperedAngle, (90.0), "Hole", App::Prop_None, "Tapered angle");
    TaperedAngle.setConstraints(&floatAngle);

    ADD_PROPERTY_TYPE(ThreadDepthType, (0L), "Hole", App::Prop_None, "Thread depth type");
    ThreadDepthType.setEnums(ThreadDepthTypeEnums);

    ADD_PROPERTY_TYPE(ThreadDepth, (23.5), "Hole", App::Prop_None, "Thread Length");  // default is
                                                                                      // assuming an M1

    ADD_PROPERTY_TYPE(
        UseCustomThreadClearance,
        (false),
        "Hole",
        App::Prop_None,
        "Use custom thread clearance"
    );

    ADD_PROPERTY_TYPE(
        CustomThreadClearance,
        (0.0),
        "Hole",
        App::Prop_None,
        "Custom thread clearance (overrides ThreadClass)"
    );
    CustomThreadClearance.setConstraints(&clearanceRange);

    // Defaults to circles & arcs so that older files are kept intact
    // while new file get points, circles and arcs set in setupObject()
    ADD_PROPERTY_TYPE(
        BaseProfileType,
        (BaseProfileTypeOptions::OnCirclesArcs),
        "Hole",
        App::Prop_None,
        "Which profile feature to base the holes on"
    );
}

void Hole::updateHoleCutParams()
{
    std::string holeCutTypeStr = HoleCutType.getValueAsString();

    // there is no cut, thus return
    if (holeCutTypeStr == "None") {
        return;
    }

    if (ThreadType.getValue() < 0) {
        throw Base::IndexError("Thread type out of range");
        return;
    }

    // get diameter and size
    double diameterVal = Diameter.getValue();

    // handle thread types
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
        if (ThreadSize.getValue() < 0) {
            throw Base::IndexError("Thread size out of range");
            return;
        }

        std::string threadSizeStr = ThreadSize.getValueAsString();

        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we take in this case the values from the norm ISO 4762 or ISO 10642
        if (holeCutTypeStr == "Counterbore") {
            // read ISO 4762 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 4762");
            const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                // there is no norm defining counterbores for all sizes, thus we need to use the
                // same fallback as for the case HoleCutTypeMap.count(key)
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                    HoleCutDepth.setValue(dimen.depth);
                }
                else {
                    calculateAndSetCounterbore();
                }
            }
            if (HoleCutDepth.getValue() == 0.0) {
                HoleCutDepth.setValue(dimen.depth);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink" || holeCutTypeStr == "Counterdrill") {
            // read ISO 10642 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 10642");
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                HoleCutCountersinkAngle.setValue(counter.angle);
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                }
                else {
                    calculateAndSetCountersink();
                }
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(counter.angle);
            }
            if (HoleCutDepth.getValue() == 0.0) {
                if (holeCutTypeStr == "Counterdrill") {
                    HoleCutDepth.setValue(1.0);
                }
                else {
                    ProfileBased::onChanged(&HoleCutDiameter);
                }
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }

        // Tag: MIGRATION
        // handle since FreeCAD 0.18 deprecated types that were
        // removed after FreeCAD 0.20
        if (holeCutTypeStr == "Cheesehead (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.6);
            HoleCutDepth.setValue(diameterVal * 0.6);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink socket screw (deprecated)") {
            HoleCutType.setValue("Countersink");
            holeCutTypeStr = "Countersink";
            HoleCutDiameter.setValue(diameterVal * 2.0);
            HoleCutDepth.setValue(diameterVal * 0.0);
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(90.0);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Cap screw (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.5);
            HoleCutDepth.setValue(diameterVal * 1.25);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }

        // cut definition
        CutDimensionKey key {threadTypeStr, holeCutTypeStr};
        if (HoleCutTypeMap.count(key)) {
            const CutDimensionSet& counter = find_cutDimensionSet(key);
            if (counter.cut_type == CutDimensionSet::Counterbore) {
                // disable HoleCutCountersinkAngle and reset it to ISO's default
                HoleCutCountersinkAngle.setValue(90.0);
                HoleCutCountersinkAngle.setReadOnly(true);
                const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
                if (dimen.thread == "None") {
                    calculateAndSetCounterbore();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of
                    // updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and
                    // read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDepth.setValue(dimen.depth);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly()) {
                            HoleCutCustomValues.setValue(false);
                        }
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
            else if (counter.cut_type == CutDimensionSet::Countersink) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                if (dimen.thread == "None") {
                    // there might be an angle of zero (if no norm exists for the size)
                    if (HoleCutCountersinkAngle.getValue() == 0.0) {
                        HoleCutCountersinkAngle.setValue(counter.angle);
                    }
                    calculateAndSetCountersink();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of
                    // updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and
                    // read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        HoleCutCountersinkAngle.setValue(counter.angle);
                        HoleCutCountersinkAngle.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly()) {
                            HoleCutCustomValues.setValue(false);
                        }
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
        }
    }
    else {
        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we use rules of thumbs as proposal
        if (holeCutTypeStr == "Counterbore") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.6);
                HoleCutDepth.setValue(diameterVal * 0.9);
            }
            if (HoleCutDepth.getValue() == 0.0) {
                HoleCutDepth.setValue(diameterVal * 0.9);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink" || holeCutTypeStr == "Counterdrill") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.7);
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutDepth.getValue() == 0.0) {
                if (holeCutTypeStr == "Counterdrill") {
                    HoleCutDepth.setValue(1.0);
                }
                else {
                    ProfileBased::onChanged(&HoleCutDiameter);
                }
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
    }
}

double Hole::getCountersinkAngle() const
{
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "BSW" || threadTypeStr == "BSF") {
        return 100.0;
    }
    if (threadTypeStr == "UNC" || threadTypeStr == "UNF" || threadTypeStr == "UNEF") {
        return 82.0;
    }
    return 90.0;
}

double Hole::getThreadClassClearance() const
{
    double pitch = getThreadPitch();

    // Calculate how much clearance to add based on Thread tolerance class and pitch
    if (ThreadClass.getValueAsString()[1] == 'G') {
        for (auto it : ThreadUtils::ThreadClass_ISOmetric_data) {
            double p = it[0];
            if (pitch <= p) {
                return it[1];
            }
        }
    }

    return 0.0;
}

// Calculates the distance between the bottom hole and the bottom most thread.
// This is defined in DIN 76-1, there are 3 possibilities:
// mode=1 (default), For normal cases e1 is wanted.
// mode=2, In cases where shorter thread runout is necessary
// mode=3, In cases where longer thread runout is necessary
double Hole::getThreadRunout(int mode) const
{
    double pitch = getThreadPitch();

    double sf = 1.0;  // scale factor
    switch (mode) {
        case 1:
            sf = 1.0;
            break;
        case 2:
            sf = 0.625;
            break;
        case 3:
            sf = 1.6;
            break;
        default:
            throw Base::ValueError("Unsupported argument");
    }
    for (auto it : ThreadUtils::ThreadRunout) {
        double p = it[0];
        if (pitch <= p) {
            return sf * it[1];
        }
    }

    // For non-standard pitch we fall back on general engineering rule of thumb of 4*pitch.
    return 4 * pitch;
}

double Hole::getThreadPitch() const
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType <= 0) {
        return 0.0;
    }
    const auto& definitions = threadUtils.getThreadDefinitions();
    const size_t typeIndex = static_cast<size_t>(threadType) - 1;
    if (typeIndex >= definitions.size()) {
        throw Base::IndexError("Thread type out of range");
    }
    const auto& pitches = definitions[typeIndex].pitches;
    if (threadSize < 0 || static_cast<size_t>(threadSize) >= pitches.size()) {
        throw Base::IndexError("Thread size out of range");
    }
    return std::stod(pitches[threadSize]);
}

void Hole::updateThreadDepthParam()
{
    std::string ThreadMethod(ThreadDepthType.getValueAsString());
    std::string HoleDepth(DepthType.getValueAsString());
    if (HoleDepth == "Dimension") {
        if (ThreadMethod == "Hole Depth") {
            ThreadDepth.setValue(Depth.getValue());
        }
        else if (ThreadMethod == "Dimension") {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > Depth.getValue()) {
                ThreadDepth.setValue(Depth.getValue());
            }
            else {
                ThreadDepth.setValue(ThreadDepth.getValue());
            }
        }
        else if (ThreadMethod == "Tapped (DIN76)") {
            ThreadDepth.setValue(Depth.getValue() - getThreadRunout());
        }
        else {
            throw Base::RuntimeError("Unsupported thread depth type \n");
        }
    }
    else if (HoleDepth == "ThroughAll") {
        if (ThreadMethod != "Dimension") {
            ThreadDepth.setValue(getThroughAllLength());
        }
        else {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > getThroughAllLength()) {
                ThreadDepth.setValue(getThroughAllLength());
            }
            else {
                ThreadDepth.setValue(ThreadDepth.getValue());
            }
        }
    }
    else {
        throw Base::RuntimeError("Unsupported depth type \n");
    }
}

std::optional<double> Hole::determineDiameter() const
{
    // Diameter parameter depends on Threaded, ThreadType, ThreadSize, and ThreadFit

    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        // When restoring the feature it might be in an inconsistent state.
        // So, just silently ignore it instead of throwing an exception.
        if (isRestoring()) {
            return std::nullopt;
        }
        throw Base::IndexError("Thread type out of range");
    }
    if (threadSize < 0) {
        // When restoring the feature it might be in an inconsistent state.
        // So, just silently ignore it instead of throwing an exception.
        if (isRestoring()) {
            return std::nullopt;
        }
        throw Base::IndexError("Thread size out of range");
    }

    if (threadType == 0) {
        return std::nullopt;
    }

    const auto& definitions = threadUtils.getThreadDefinitions();
    const size_t typeIndex = static_cast<size_t>(threadType) - 1;
    if (typeIndex >= definitions.size()
        || static_cast<size_t>(threadSize) >= definitions[typeIndex].designations.size()) {
        if (isRestoring()) {
            return std::nullopt;
        }
        throw Base::IndexError("Thread size out of range");
    }
    const auto& definition = definitions[typeIndex];
    double diameter = std::stod(definition.sizes[threadSize]);
    double pitch = std::stod(definition.pitches[threadSize]);
    double tapDrill = std::stod(definition.tapDrills[threadSize]);
    double clearance = 0.0;

    if (Threaded.getValue()) {

        if (ModelThread.getValue()) {
            if (UseCustomThreadClearance.getValue()) {
                clearance = CustomThreadClearance.getValue();
            }
            else {
                clearance = getThreadClassClearance();
            }
        }

        // use normed diameters if possible
        std::string threadTypeStr = ThreadType.getValueAsString();
        if (tapDrill > 0) {
            diameter = tapDrill + clearance;
        }  // if nothing is available, we must calculate
        else if (threadTypeStr == "BSP" || threadTypeStr == "BSW" || threadTypeStr == "BSF") {
            double thread = 2 * (0.640327 * pitch);
            // truncation is allowed by ISO-228 and BS 84
            diameter = diameter - thread * 0.75 + clearance;
        }
        else if (threadTypeStr == "NPT") {
            double thread = 2 * (0.8 * pitch);
            diameter = diameter - thread * 0.75 + clearance;
        }
        else {
            // this fits exactly the definition for ISO metric fine
            diameter = diameter - pitch + clearance;
        }
    }
    else {  // we have a clearance hole
        bool found = false;
        std::string threadTypeStr = ThreadType.getValueAsString();
        // UTS and metric have a different clearance hole set
        if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
            int MatrixRowSizeMetric = sizeof(metricHoleDiameters) / sizeof(metricHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
                case 0: /* standard fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][2];
                            found = true;
                            break;
                        }
                    }
                    // if nothing is defined (e.g. for M2.2, M9 and M11), we must calculate
                    // we use the factors defined for M5 in the metricHoleDiameters list
                    if (!found) {
                        diameter = diameter * 1.10;
                    }
                    break;
                case 1: /* close fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][1];
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.06;
                    }
                    break;
                case 2: /* wide fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][3];
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.16;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
        else if (threadTypeStr == "UNC" || threadTypeStr == "UNF" || threadTypeStr == "UNEF") {
            std::string ThreadSizeString = ThreadSize.getValueAsString();
            int MatrixRowSizeUTS = sizeof(UTSHoleDiameters) / sizeof(UTSHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
                case 0: /* normal fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].normal;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found (if "#12" or "9/16"), we must calculate
                    // // we use the factors defined for "3/8" in the UTSHoleDiameters list
                    if (!found) {
                        diameter = diameter * 1.08;
                    }
                    break;
                case 1: /* close fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].close;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.04;
                    }
                    break;
                case 2: /* loose fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].loose;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.12;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
        else {
            switch (ThreadFit.getValue()) {
                case 0: /* normal fit */
                    // we must calculate
                    if (!found) {
                        diameter = diameter * 1.1;
                    }
                    break;
                case 1: /* close fit */
                    if (!found) {
                        diameter = diameter * 1.05;
                    }
                    break;
                case 2: /* loose fit */
                    if (!found) {
                        diameter = diameter * 1.15;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
    }

    return std::optional<double> {diameter};
}

void Hole::updateDiameterParam()
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType > 0 && threadSize > 0) {
        const auto& definitions = threadUtils.getThreadDefinitions();
        const size_t typeIndex = static_cast<size_t>(threadType) - 1;
        if (typeIndex < definitions.size()
            && static_cast<size_t>(threadSize) < definitions[typeIndex].sizes.size()) {
            ThreadDiameter.setValue(std::stod(definitions[typeIndex].sizes[threadSize]));
        }
    }
    if (auto opt = determineDiameter()) {
        Diameter.setValue(opt.value());
    }
}

double Hole::getThreadProfileAngle()
{
    // Both ISO 7-1 and ASME B1.20.1 define the same angle
    return 90 - 1.79;
}

void Hole::findClosestDesignation()
{
    int threadType = ThreadType.getValue();

    if (threadType <= 0) {
        return;
    }
    const auto& definitions = threadUtils.getThreadDefinitions();
    const size_t typeIndex = static_cast<size_t>(threadType) - 1;
    if (typeIndex >= definitions.size()) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread type is invalid"));
    }

    double diameter = ThreadDiameter.getValue();
    if (diameter == 0.0) {
        diameter = Diameter.getValue();
    }

    struct SizeOption
    {
        double diameter;
        double pitch;
    };
    const auto& definition = definitions[typeIndex];
    std::vector<SizeOption> options;
    options.reserve(definition.designations.size());
    for (size_t row = 0; row < definition.designations.size(); ++row) {
        options.push_back({std::stod(definition.sizes[row]), std::stod(definition.pitches[row])});
    }

    int oldSizeIndex = ThreadSize.getValue();
    double targetPitch = 0.0;
    if (oldSizeIndex >= 0 && oldSizeIndex < static_cast<int>(options.size())) {
        targetPitch = options[oldSizeIndex].pitch;
    }
    size_t bestIndex = 0;
    if (targetPitch == 0.0) {
        // If pitch is unknown, prioritize the closest diameter
        double bestDiameterDiff = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < options.size(); ++i) {
            double dDiff = std::abs(options[i].diameter - diameter);
            if (dDiff < bestDiameterDiff) {
                bestDiameterDiff = dDiff;
                bestIndex = i;
            }
        }
    }
    else {
        // Scan all entries to find the minimal (Δdiameter, Δpitch) Euclidean distance
        double bestMetric = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < options.size(); ++i) {
            double dDiff = options[i].diameter - diameter;
            double pDiff = options[i].pitch - targetPitch;
            double metric = std::hypot(dDiff, pDiff);
            if (metric < bestMetric) {
                bestMetric = metric;
                bestIndex = i;
            }
        }
    }

    ThreadSize.setValue(static_cast<int>(bestIndex));
}

void Hole::onChanged(const App::Property* prop)
{
    if (prop == &ThreadType) {
        std::string type;

        if (ThreadType.isValid()) {
            type = ThreadType.getValueAsString();
            ThreadSize.setEnums(getThreadDesignations(threadUtils, ThreadType.getValue()));
            if (type != "None") {
                findClosestDesignation();
            }
        }

        if (type == "None") {
            ThreadClass.setEnums(ThreadUtils::ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
            Threaded.setValue(false);
            ModelThread.setValue(false);
            UseCustomThreadClearance.setValue(false);
            ThreadFit.setEnums(ClearanceNoneEnums);
        }
        else if (type == "ISOMetricProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetric_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetric_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "ISOMetricFineProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetricfine_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetricfine_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "UNC") {
            ThreadClass.setEnums(ThreadClass_UNC_Enums);
            HoleCutType.setEnums(HoleCutType_UNC_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "UNF") {
            ThreadClass.setEnums(ThreadClass_UNF_Enums);
            HoleCutType.setEnums(HoleCutType_UNF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "UNEF") {
            ThreadClass.setEnums(ThreadClass_UNEF_Enums);
            HoleCutType.setEnums(HoleCutType_UNEF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "BSP") {
            ThreadClass.setEnums(ThreadUtils::ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_BSP_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "NPT") {
            ThreadClass.setEnums(ThreadUtils::ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_NPT_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "BSW") {
            ThreadClass.setEnums(ThreadClass_BSW_Enums);
            HoleCutType.setEnums(HoleCutType_BSW_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
        }
        else if (type == "BSF") {
            ThreadClass.setEnums(ThreadClass_BSF_Enums);
            HoleCutType.setEnums(HoleCutType_BSF_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
        }
        else if (type == "ISOTyre") {
            ThreadClass.setEnums(ThreadUtils::ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
        }

        bool isNone = type == "None";
        bool isThreaded = Threaded.getValue();

        Diameter.setReadOnly(!isNone);
        Threaded.setReadOnly(isNone);
        ThreadSize.setReadOnly(isNone);
        ThreadFit.setReadOnly(isNone || isThreaded);
        ThreadClass.setReadOnly(isNone || !isThreaded);
        ThreadDepthType.setReadOnly(isNone || !isThreaded);
        ThreadDepth.setReadOnly(isNone || !isThreaded);
        ModelThread.setReadOnly(isNone || !isThreaded);
        UseCustomThreadClearance.setReadOnly(isNone || !isThreaded || !ModelThread.getValue());
        CustomThreadClearance.setReadOnly(
            !UseCustomThreadClearance.getValue() || UseCustomThreadClearance.isReadOnly()
        );

        std::string holeCutTypeStr;
        if (HoleCutType.isValid()) {
            std::string holeCutTypeStr = HoleCutType.getValueAsString();
        }
        if (holeCutTypeStr == "None") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(true);
            HoleCutDepth.setReadOnly(true);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Counterbore") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else {  // screw definition
            HoleCutCustomValues.setReadOnly(false);
            if (HoleCutCustomValues.getValue()) {
                HoleCutDiameter.setReadOnly(false);
                HoleCutDepth.setReadOnly(false);
                // we must not set HoleCutCountersinkAngle here because the info if this can
                // be enabled is first available in updateHoleCutParams and thus handled there
                updateHoleCutParams();
            }
            else {
                HoleCutDiameter.setReadOnly(true);
                HoleCutDepth.setReadOnly(true);
                HoleCutCountersinkAngle.setReadOnly(true);
            }
        }

        // Signal changes to these
        ProfileBased::onChanged(&ThreadSize);
        ProfileBased::onChanged(&ThreadClass);
        ProfileBased::onChanged(&HoleCutType);
        ProfileBased::onChanged(&Threaded);

        // Diameter parameter depends on this
        if (type != "None") {
            updateDiameterParam();
        }
    }
    else if (prop == &Threaded) {
        std::string type(ThreadType.getValueAsString());

        // thread class and direction are only sensible if threaded
        // fit only sensible if not threaded
        if (Threaded.getValue()) {
            CosmeticThread.setReadOnly(false);
            ThreadClass.setReadOnly(false);
            ThreadDirection.setReadOnly(false);
            ThreadFit.setReadOnly(true);
            ModelThread.setReadOnly(false);
            UseCustomThreadClearance.setReadOnly(false);
            CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(false);
            ThreadDepth.setReadOnly(std::string(ThreadDepthType.getValueAsString()) != "Dimension");
            if (Tapered.getValue() && TaperedAngle.getValue() == 90) {
                TaperedAngle.setValue(getThreadProfileAngle());
            }
        }
        else {
            CosmeticThread.setValue(false);
            CosmeticThread.setReadOnly(true);
            ThreadClass.setReadOnly(true);
            ThreadDirection.setReadOnly(true);
            if (type == "None") {
                ThreadFit.setReadOnly(true);
            }
            else {
                ThreadFit.setReadOnly(false);
            }
            ModelThread.setReadOnly(true);
            UseCustomThreadClearance.setReadOnly(true);
            CustomThreadClearance.setReadOnly(true);
            ThreadDepthType.setReadOnly(true);
            ThreadDepth.setReadOnly(true);
        }

        // Diameter parameter depends on this
        updateDiameterParam();
    }
    else if (prop == &ModelThread) {
        // Diameter parameter depends on this
        updateDiameterParam();
        UseCustomThreadClearance.setReadOnly(!ModelThread.getValue());
        if (CosmeticThread.getValue() && ModelThread.getValue()) {
            CosmeticThread.setValue(false);
        }
    }
    else if (prop == &CosmeticThread) {
        if (CosmeticThread.getValue() && ModelThread.getValue()) {
            ModelThread.setValue(false);
        }
    }
    else if (prop == &DrillPoint) {
        if (DrillPoint.getValue() == 1) {
            DrillPointAngle.setReadOnly(false);
            DrillForDepth.setReadOnly(false);
        }
        else {
            DrillPointAngle.setReadOnly(true);
            DrillForDepth.setReadOnly(true);
        }
    }
    else if (prop == &Tapered) {
        if (Tapered.getValue()) {
            TaperedAngle.setReadOnly(false);
            if (Threaded.getValue() && TaperedAngle.getValue() == 90) {
                TaperedAngle.setValue(getThreadProfileAngle());
            }
        }
        else {
            TaperedAngle.setValue(90);
            TaperedAngle.setReadOnly(true);
        }
    }
    else if (prop == &ThreadSize) {
        updateDiameterParam();
        if (!isRestoring()) {
            updateThreadDepthParam();
        }
        // updateHoleCutParams() will later automatically be called because
    }
    else if (prop == &ThreadFit) {
        updateDiameterParam();
    }
    else if (prop == &Diameter) {
        // a changed diameter means we also need to check the hole cut
        // because the hole cut diameter must not be <= than the diameter
        updateHoleCutParams();
        if (ThreadType.getValue() == 0) {
            // Profile is None but this is needed to find the closest
            // designation if the user switch to threaded
            ThreadDiameter.setValue(Diameter.getValue());
        }
    }
    else if (prop == &HoleCutType) {
        // the read-only states are set in updateHoleCutParams()
        updateHoleCutParams();
        ProfileBased::onChanged(&HoleCutDiameter);
        ProfileBased::onChanged(&HoleCutDepth);
        ProfileBased::onChanged(&HoleCutCountersinkAngle);
    }
    else if (prop == &HoleCutCustomValues) {
        // when going back to standardized values, we must recalculate
        // also to find out if HoleCutCountersinkAngle can be ReadOnly
        // both an also the read-only states is done in updateHoleCutParams()
        updateHoleCutParams();
    }
    else if (prop == &HoleCutDiameter || prop == &HoleCutCountersinkAngle) {
        // Recalculate depth if Countersink
        const std::string holeCutTypeString = HoleCutType.getValueAsString();
        const std::string threadTypeString = ThreadType.getValueAsString();
        if (!(holeCutTypeString == "Countersink"
              || isDynamicCountersink(threadTypeString, holeCutTypeString))) {
            return;
        }
        auto angle = Base::toRadians(HoleCutCountersinkAngle.getValue());
        constexpr double fallback = 2.0;
        constexpr double EPSILON = 1e-6;
        if (angle <= 0.0 || angle >= std::numbers::pi) {
            HoleCutDepth.setValue(fallback);
        }
        else {
            double tanHalfAngle = std::tan(angle / 2.0);
            if (std::abs(tanHalfAngle) < EPSILON) {
                // Avoid near-zero division
                HoleCutDepth.setValue(fallback);
            }
            else {
                double diameter = HoleCutDiameter.getValue();
                HoleCutDepth.setValue((diameter / 2.0) / tanHalfAngle);
            }
        }
        ProfileBased::onChanged(&HoleCutDepth);
    }
    else if (prop == &DepthType) {
        std::string DepthMode(DepthType.getValueAsString());
        bool isNotDimension = (DepthMode != "Dimension");

        Depth.setReadOnly(isNotDimension);
        DrillPoint.setReadOnly(isNotDimension);
        DrillPointAngle.setReadOnly(isNotDimension);
        DrillForDepth.setReadOnly(isNotDimension);

        if (!isRestoring()) {
            if (isNotDimension) {
                // if through all, set the depth accordingly
                Depth.setValue(getThroughAllLength());
            }
            updateThreadDepthParam();
        }
    }
    else if (prop == &Depth) {
        if (!isRestoring()) {
            // the depth cannot be greater than the through-all length
            if (Depth.getValue() > getThroughAllLength()) {
                Depth.setValue(getThroughAllLength());
            }
        }

        if (std::string(ThreadDepthType.getValueAsString()) != "Dimension") {
            updateDiameterParam();  // make sure diameter and pitch are updated.
        }

        if (!isRestoring()) {
            updateThreadDepthParam();
        }
    }
    else if (prop == &ThreadDepthType) {
        if (!isRestoring()) {
            updateThreadDepthParam();
        }
        ThreadDepth.setReadOnly(
            Threaded.getValue() && std::string(ThreadDepthType.getValueAsString()) != "Dimension"
        );
    }
    else if (prop == &ThreadDepth) {
        // the thread depth cannot be greater than the hole depth
        if (ThreadDepth.getValue() > Depth.getValue()) {
            ThreadDepth.setValue(Depth.getValue());
        }
    }
    else if (prop == &UseCustomThreadClearance) {
        updateDiameterParam();
        CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
    }
    else if (prop == &CustomThreadClearance) {
        updateDiameterParam();
    }

    ProfileBased::onChanged(prop);
}
void Hole::setupObject()
{
    // Set the BaseProfileType to the user defined value
    // here so that new objects use points, but older files
    // keep the default value of "Circles and Arcs"

    Base::Reference<ParameterGrp> hGrp = App::GetApplication()
                                             .GetUserParameter()
                                             .GetGroup("BaseApp")
                                             ->GetGroup("Preferences")
                                             ->GetGroup("Mod/PartDesign");

    BaseProfileType.setValue(baseProfileOption_idxToBitmask(hGrp->GetInt("defaultBaseTypeHole", 1)));

    ProfileBased::setupObject();
}

/**
 * Computes 2D intersection between the lines (pa1, pa2) and (pb1, pb2).
 * The lines are assumed to be crossing, and it is an error
 * to specify parallel lines.
 * Only the x and y coordinates of the points are used to compute the 2D intersection.
 *
 * The result are the x and y coordinate of the intersection point.
 */
static void computeIntersection(gp_Pnt pa1, gp_Pnt pa2, gp_Pnt pb1, gp_Pnt pb2, double& x, double& y)
{
    double vx1 = pa1.X() - pa2.X();
    double vy1 = pa1.Y() - pa2.Y();
    double vx2 = pb1.X() - pb2.X();
    double vy2 = pb1.Y() - pb2.Y();
    double x1 = pa1.X();
    double y1 = pa1.Y();
    double x2 = pb1.X();
    double y2 = pb1.Y();

    /* Solve the system
        x1 + t1 * vx1 = x2 + t2 * vx2
        y1 + t1 * vy1 = y2 + t2 * vy2

        =>

       [ vx1    -vx2 ] [ t1 ] = [ x2 - x1 ]
       [ vy1    -vy2 ] [ t2 ] = [ y2 - y1 ]

       =>

       [ t1 ] = f * [ -vy2  vx2 ] [ x2 - x1 ]
       [ t2 ] =     [ -vy1  vx1 ] [ y2 - y1 ]

     */

    assert(((vx1 * -vy2) - (-vx2 * vy1)) != 0);

    double f = 1 / ((vx1 * -vy2) - (-vx2 * vy1));

    double t1 = -vy2 * f * (x2 - x1) + vx2 * f * (y2 - y1);

#ifdef _DEBUG
    double t2 = -vy1 * f * (x2 - x1) + vx1 * f * (y2 - y1);

    assert((x1 + t1 * vx1) - (x2 + t2 * vx2) < 1e-6);
    assert((y1 + t1 * vy1) - (y2 + t2 * vy2) < 1e-6);
#endif

    x = x1 + t1 * vx1;
    y = y1 + t1 * vy1;
}

short Hole::mustExecute() const
{
    if (ThreadType.isTouched() || Threaded.isTouched() || ThreadSize.isTouched()
        || ThreadClass.isTouched() || ThreadFit.isTouched() || Diameter.isTouched()
        || ThreadDirection.isTouched() || HoleCutType.isTouched() || HoleCutDiameter.isTouched()
        || HoleCutDepth.isTouched() || HoleCutCountersinkAngle.isTouched() || DepthType.isTouched()
        || Depth.isTouched() || DrillPoint.isTouched() || DrillPointAngle.isTouched()
        || Tapered.isTouched() || TaperedAngle.isTouched() || ModelThread.isTouched()
        || UseCustomThreadClearance.isTouched() || CustomThreadClearance.isTouched()
        || ThreadDepthType.isTouched() || ThreadDepth.isTouched() || BaseProfileType.isTouched()) {
        return 1;
    }
    return ProfileBased::mustExecute();
}

void Hole::Restore(Base::XMLReader& reader)
{
    ProfileBased::Restore(reader);
    updateProps();
}

void Hole::updateProps()
{
    onChanged(&Threaded);
    onChanged(&ThreadType);
    onChanged(&ThreadSize);
    onChanged(&ThreadClass);
    onChanged(&ThreadFit);
    onChanged(&Diameter);
    onChanged(&ThreadDirection);
    onChanged(&HoleCutType);
    onChanged(&HoleCutDiameter);
    onChanged(&HoleCutDepth);
    onChanged(&HoleCutCountersinkAngle);
    onChanged(&DepthType);
    onChanged(&Depth);
    onChanged(&DrillPoint);
    onChanged(&DrillPointAngle);
    onChanged(&Tapered);
    onChanged(&TaperedAngle);
    onChanged(&ModelThread);
    onChanged(&UseCustomThreadClearance);
    onChanged(&CustomThreadClearance);
    onChanged(&ThreadDepthType);
    onChanged(&ThreadDepth);
    onChanged(&BaseProfileType);
}

static gp_Pnt toPnt(gp_Vec dir)
{
    return {dir.X(), dir.Y(), dir.Z()};
}

App::DocumentObjectExecReturn* Hole::execute()
{
    TopoShape profileshape = getProfileShape(
        Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform | Part::ShapeOption::DontSimplifyCompound
    );

    // Find the base shape
    TopoShape base;
    try {
        base = getBaseTopoShape();
    }
    catch (const Base::Exception&) {
        std::string text(QT_TRANSLATE_NOOP(
            "Exception",
            "The requested feature cannot be created. The reason may be that:\n"
            "  - the active Body does not contain a base shape, so there is no\n"
            "  material to be removed;\n"
            "  - the selected sketch does not belong to the active Body."
        ));
        return new App::DocumentObjectExecReturn(text);
    }

    try {
        if (Diameter.getValue() < diameterRange.LowerBound) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Diameter too small")
            );
        }
        std::string method(DepthType.getValueAsString());
        double length = 0.0;

        this->positionByPrevious();
        TopLoc_Location invObjLoc = this->getLocation().Inverted();

        base.move(invObjLoc);
        profileshape.move(invObjLoc);

        /* Build the prototype hole */

        // Get vector normal to profile
        Base::Vector3d SketchVector = guessNormalDirection(profileshape);

        if (Reversed.getValue()) {
            SketchVector *= -1.0;
        }

        // Define this as zDir
        gp_Vec zDir(SketchVector.x, SketchVector.y, SketchVector.z);
        zDir.Transform(invObjLoc.Transformation());
        gp_Vec xDir = threadUtils.computePerpendicular(zDir);

        if (method == "Dimension") {
            length = Depth.getValue();
        }
        else if (method == "UpToFirst") {
            /* TODO */
        }
        else if (method == "ThroughAll") {
            length = getThroughAllLength();
        }
        else {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Unsupported length specification")
            );
        }

        if (length <= 0.0) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid hole depth")
            );
        }

        BRepBuilderAPI_MakeWire mkWire;
        const std::string holeCutType = HoleCutType.getValueAsString();
        const std::string threadType = ThreadType.getValueAsString();
        bool isCountersink
            = (holeCutType == "Countersink" || isDynamicCountersink(threadType, holeCutType));
        bool isCounterbore
            = (holeCutType == "Counterbore" || isDynamicCounterbore(threadType, holeCutType));
        bool isCounterdrill = (holeCutType == "Counterdrill");

        double TaperedAngleVal = Tapered.getValue() ? Base::toRadians(TaperedAngle.getValue())
                                                    : Base::toRadians(90.0);
        double radiusBottom = Diameter.getValue() / 2.0 - length / tan(TaperedAngleVal);

        double radius = Diameter.getValue() / 2.0;
        gp_Pnt firstPoint(0, 0, 0);
        gp_Pnt lastPoint(0, 0, 0);
        double lengthCounter = 0.0;
        double xPosCounter = 0.0;
        double zPosCounter = 0.0;

        if (TaperedAngleVal <= 0.0 || TaperedAngleVal > Base::toRadians(180.0)) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid taper angle")
            );
        }

        if (isCountersink || isCounterbore || isCounterdrill) {
            double holeCutRadius = HoleCutDiameter.getValue() / 2.0;
            double holeCutDepth = HoleCutDepth.getValue();
            double countersinkAngle = Base::toRadians(HoleCutCountersinkAngle.getValue() / 2.0);

            if (isCounterbore) {
                // Counterbore is rendered the same way as a countersink, but with a hardcoded
                // angle of 90deg
                countersinkAngle = Base::toRadians(90.0);
            }

            if (isCountersink) {
                holeCutDepth = 0.0;
                // We cannot recalculate the HoleCutDiameter because the previous HoleCutDepth
                // is unknown. Therefore we cannot know with what HoleCutDepth the current
                // HoleCutDiameter was calculated.
            }

            if (holeCutRadius < radius) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Hole cut diameter too small")
                );
            }

            if (holeCutDepth > length) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                    "Exception",
                    "Hole error: Hole cut depth must be less than hole depth"
                ));
            }

            if (holeCutDepth < 0.0) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                    "Exception",
                    "Hole error: Hole cut depth must be greater or equal to zero"
                ));
            }

            // Top point
            gp_Pnt newPoint = toPnt(holeCutRadius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            // Bottom of counterbore
            if (holeCutDepth > 0.0) {
                newPoint = toPnt(holeCutRadius * xDir - holeCutDepth * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }

            // Compute intersection of tapered edge and line at bottom of counterbore hole
            computeIntersection(
                gp_Pnt(holeCutRadius, -holeCutDepth, 0),
                gp_Pnt(holeCutRadius - sin(countersinkAngle), -cos(countersinkAngle) - holeCutDepth, 0),
                gp_Pnt(radius, 0, 0),
                gp_Pnt(radiusBottom, -length, 0),
                xPosCounter,
                zPosCounter
            );

            if (-length > zPosCounter) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid countersink")
                );
            }

            lengthCounter = zPosCounter;
            newPoint = toPnt(xPosCounter * xDir + zPosCounter * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else {
            gp_Pnt newPoint = toPnt(radius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
            lengthCounter = 0.0;
        }

        std::string drillPoint = DrillPoint.getValueAsString();
        double xPosDrill = 0.0;
        double zPosDrill = 0.0;
        if (drillPoint == "Flat") {
            gp_Pnt newPoint = toPnt(radiusBottom * xDir + -length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            newPoint = toPnt(-length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else if (drillPoint == "Angled") {
            double drillPointAngle = Base::toRadians((180.0 - DrillPointAngle.getValue()) / 2.0);
            gp_Pnt newPoint;
            bool isDrillForDepth = DrillForDepth.getValue();

            // the angle is in any case > 0 and < 90 but nevertheless this safeguard:
            if (drillPointAngle <= 0.0 || drillPointAngle >= Base::toRadians(180.0)) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point angle")
                );
            }

            // if option to take drill point size into account
            // the next wire point is the intersection of the drill edge and the hole edge
            if (isDrillForDepth) {
                computeIntersection(
                    gp_Pnt(0, -length, 0),
                    gp_Pnt(radius, radius * tan(drillPointAngle) - length, 0),
                    gp_Pnt(radius, 0, 0),
                    gp_Pnt(radiusBottom, -length, 0),
                    xPosDrill,
                    zPosDrill
                );
                if (zPosDrill > 0 || zPosDrill >= lengthCounter) {
                    return new App::DocumentObjectExecReturn(
                        QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point")
                    );
                }

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                newPoint = toPnt(-length * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
            else {
                xPosDrill = radiusBottom;
                zPosDrill = -length;

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                // the end point is the size of the drill tip
                newPoint = toPnt((-length - radius * tan(drillPointAngle)) * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
        }

        mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, firstPoint));

        TopoDS_Wire wire = mkWire.Wire();

        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

        double angle = Base::toRadians<double>(360.0);
        BRepPrimAPI_MakeRevol RevolMaker(face, gp_Ax1(firstPoint, zDir), angle);
        if (!RevolMaker.IsDone()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Could not revolve sketch")
            );
        }

        TopoDS_Shape protoHole = RevolMaker.Shape();
        if (protoHole.IsNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Resulting shape is empty")
            );
        }

        // Make thread
        if (Threaded.getValue() && ModelThread.getValue()) {
            TopoDS_Shape protoThread = makeThread(xDir, zDir, length);

            TopoDS_Compound holeWithThread;
            holeWithThread.Nullify();

            BRep_Builder builder;
            builder.MakeCompound(holeWithThread);
            builder.Add(holeWithThread, protoHole);
            builder.Add(holeWithThread, protoThread);

            // we reuse the name protoHole (only now it is threaded)
            protoHole = holeWithThread;
        }
        std::vector<TopoShape> holes;
        auto compound = findHoles(holes, profileshape, protoHole);
        if (holes.empty()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Finding axis failed")
            );
        }

        TopoShape result(0);

        // set the subtractive shape property for later usage in e.g. pattern
        this->AddSubShape.setValue(compound);

        if (base.isNull()) {
            Shape.setValue(compound);
            return App::DocumentObject::StdReturn;
        }

        // First try cutting with compound which will be faster as it is done in
        // parallel
        bool retry = true;
        const char* maker;
        switch (getAddSubType()) {
            case Additive:
                maker = Part::OpCodes::Fuse;
                break;
            default:
                maker = Part::OpCodes::Cut;
        }
        try {
            if (base.isNull()) {
                result = compound;
            }
            else {
                result.makeElementBoolean(maker, {base, compound}, nullptr, FuzzyTolerance.getValue());
            }
            result = getSolid(result);
            retry = false;
        }
        catch (Standard_Failure& e) {
            FC_WARN(
                getFullName() << ": boolean operation with compound failed ("
                              << e.GetMessageString() << "), retry…"
            );
        }
        catch (Base::Exception& e) {
            FC_WARN(
                getFullName() << ": boolean operation with compound failed (" << e.what() << "), retry…"
            );
        }

        if (retry) {
            int i = 0;
            for (auto& hole : holes) {
                ++i;
                try {
                    result.makeElementBoolean(maker, {base, hole}, nullptr, FuzzyTolerance.getValue());
                }
                catch (Standard_Failure&) {
                    std::string msg(
                        QT_TRANSLATE_NOOP("Exception", "Boolean operation failed on profile Edge")
                    );
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
                catch (Base::Exception& e) {
                    e.reportException();
                    std::string msg(
                        QT_TRANSLATE_NOOP("Exception", "Boolean operation failed on profile Edge")
                    );
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
                base = getSolid(result);
                if (base.isNull()) {
                    std::string msg(QT_TRANSLATE_NOOP(
                        "Exception",
                        "Boolean operation produced non-solid on profile Edge"
                    ));
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
            }
            result = base;
        }
        result = refineShapeIfActive(result);

        if (!isSingleSolidRuleSatisfied(result.getShape())) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                "Exception",
                "Result has multiple solids: enable 'Allow Compound' in the active body."
            ));
        }
        this->Shape.setValue(result);

        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        if (std::string(e.GetMessageString()) == "TopoDS::Face"
            && (std::string(DepthType.getValueAsString()) == "UpToFirst"
                || std::string(DepthType.getValueAsString()) == "UpToFace")) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                "Exception",
                "Could not create face from sketch.\n"
                "Intersecting sketch entities or multiple faces in a sketch are not allowed "
                "for making a pocket up to a face."
            ));
        }
        else {
            return new App::DocumentObjectExecReturn(e.GetMessageString());
        }
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
}

Base::Vector3d Hole::guessNormalDirection(const TopoShape& profileshape) const
{
    // If trying to build a hole from a cylinder face
    // we must try to find the direction ourselves as
    // getProfileNormal() will try to find the normal to
    // the middle of the face
    if (profileshape.hasSubShape(TopAbs_FACE)) {
        BRepAdaptor_Surface sf(TopoDS::Face(profileshape.getSubShape(TopAbs_FACE, 1)));

        if (sf.GetType() == GeomAbs_Cylinder) {
            return Base::convertTo<Base::Vector3d>(sf.Cylinder().Axis().Direction());
        }
    }

    return getProfileNormal();
}

TopoShape Hole::findHoles(
    std::vector<TopoShape>& holes,
    const TopoShape& profileshape,
    const TopoDS_Shape& protoHole
) const
{
    TopoShape result(0);
    _holeLocations.clear();

    auto addHole = [&](Part::TopoShape const& baseshape, gp_Pnt loc) {
        _holeLocations.push_back(loc);
        gp_Trsf localSketchTransformation;
        localSketchTransformation.SetTranslation(gp_Pnt(0, 0, 0), gp_Pnt(loc.X(), loc.Y(), loc.Z()));

        Part::ShapeMapper mapper;
        mapper.populate(
            Part::MappingStatus::Modified,
            baseshape,
            TopoShape(protoHole).getSubTopoShapes(TopAbs_FACE)
        );

        TopoShape hole(-getID());
        hole.makeShapeWithElementMap(protoHole, mapper, {baseshape});

        // transform and generate element map.
        hole = hole.makeElementTransform(localSketchTransformation);
        holes.push_back(hole);
    };

    int baseProfileType = BaseProfileType.getValue();

    // Iterate over edges and filter out non-circle/non-arc types
    if (baseProfileType & BaseProfileTypeOptions::OnCircles
        || baseProfileType & BaseProfileTypeOptions::OnArcs) {
        for (const auto& profileEdge : profileshape.getSubTopoShapes(TopAbs_EDGE)) {
            TopoDS_Edge edge = TopoDS::Edge(profileEdge.getShape());
            BRepAdaptor_Curve adaptor(edge);

            // Circle base?
            if (adaptor.GetType() != GeomAbs_Circle) {
                continue;
            }
            // Filter for circles
            if (!(baseProfileType & BaseProfileTypeOptions::OnCircles) && adaptor.IsClosed()) {
                continue;
            }

            // Filter for arcs
            if (!(baseProfileType & BaseProfileTypeOptions::OnArcs) && !adaptor.IsClosed()) {
                continue;
            }

            gp_Circ circle = adaptor.Circle();
            addHole(profileEdge, circle.Axis().Location());
        }
    }

    // To avoid breaking older files which where not made with
    // holes on points
    if (baseProfileType & BaseProfileTypeOptions::OnPoints) {
        // Iterate over vertices while avoiding edges so that curve handles are ignored
        for (const auto& profileVertex : profileshape.getSubTopoShapes(TopAbs_VERTEX, TopAbs_EDGE)) {
            TopoDS_Vertex vertex = TopoDS::Vertex(profileVertex.getShape());

            addHole(profileVertex, BRep_Tool::Pnt(vertex));
        }
    }
    return TopoShape().makeElementCompound(holes);
}

std::vector<gp_Pnt> Hole::getHoleLocations() const
{
    return _holeLocations;
}

TopoDS_Shape Hole::makeThread(const gp_Vec& xDir, const gp_Vec& zDir, double length)
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread type out of range"));
    }
    if (threadSize < 0) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread size out of range"));
    }

    bool leftHanded = (bool)ThreadDirection.getValue();

    // Nomenclature and formulae according to Figure 1 of ISO 68-1
    // this is the same for all metric and UTS threads as stated here:
    // https://en.wikipedia.org/wiki/File:ISO_and_UTS_Thread_Dimensions.svg
    // Rmaj is half of the major diameter
    const auto& definitions = threadUtils.getThreadDefinitions();
    const size_t typeIndex = static_cast<size_t>(threadType) - 1;
    if (threadType == 0 || typeIndex >= definitions.size()
        || static_cast<size_t>(threadSize) >= definitions[typeIndex].sizes.size()) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread size out of range"));
    }
    double Rmaj = std::stod(definitions[typeIndex].sizes[threadSize]) / 2;
    double Pitch = getThreadPitch();

    double clearance;  // clearance to be added on the diameter
    if (UseCustomThreadClearance.getValue()) {
        clearance = CustomThreadClearance.getValue() / 2;
    }
    else {
        clearance = getThreadClassClearance() / 2;
    }
    double RmajC = Rmaj + clearance;

    const Part::TopoShape threadProfile = threadUtils.makeLegacyThreadProfile(
        ThreadType.getValueAsString(),
        RmajC,
        Pitch,
        xDir,
        zDir
    );
    TopoDS_Wire threadWire = TopoDS::Wire(threadProfile.getShape());

    // create the helix path
    double threadDepth = ThreadDepth.getValue();
    double helixLength = threadDepth + Pitch / 2;
    double holeDepth = Depth.getValue();
    std::string threadDepthMethod(ThreadDepthType.getValueAsString());
    std::string depthMethod(DepthType.getValueAsString());
    if (threadDepthMethod != "Dimension") {
        if (depthMethod == "ThroughAll") {
            threadDepth = length;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + 2 * Pitch;
        }
        else if (threadDepthMethod == "Tapped (DIN76)") {
            threadDepth = holeDepth - getThreadRunout();
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 2;
        }
        else {  // Hole depth
            threadDepth = holeDepth;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 8;
        }
    }
    else {
        if (depthMethod == "Dimension") {
            // the thread must not be deeper than the hole
            // thus the max helixLength is holeDepth + P / 8;
            if (threadDepth > (holeDepth - Pitch / 2)) {
                helixLength = holeDepth + Pitch / 8;
            }
        }
    }
    double helixAngle = Tapered.getValue() ? TaperedAngle.getValue() - 90 : 0.0;

    const ThreadUtils::ThreadSweep sweep {Pitch, Rmaj, helixLength, helixAngle, leftHanded};
    return threadUtils.makeThreadSolid(threadWire, sweep, zDir);
}

void Hole::addCutType(const CutDimensionSet& dimensions)
{
    const CutDimensionSet::ThreadType thread = dimensions.thread_type;
    const std::string& name = dimensions.name;

    std::vector<std::string>* list;
    switch (thread) {
        case CutDimensionSet::Metric:
            HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricProfile", name), dimensions);
            list = &HoleCutType_ISOmetric_Enums;
            break;
        case CutDimensionSet::MetricFine:
            HoleCutTypeMap.emplace(CutDimensionKey("ISOMetricFineProfile", name), dimensions);
            list = &HoleCutType_ISOmetricfine_Enums;
            break;
        default:
            return;
    }
    // add the collected lists of JSON definitions to the lists
    // if a name doesn't already exist in the list
    if (std::all_of(list->begin(), list->end(), [name](const std::string& x) { return x != name; })) {
        list->push_back(name);
    }
}

bool Hole::isDynamicCounterbore(const std::string& thread, const std::string& holeCutType)
{
    CutDimensionKey key {thread, holeCutType};
    return HoleCutTypeMap.count(key)
        && HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Counterbore;
}

bool Hole::isDynamicCountersink(const std::string& thread, const std::string& holeCutType)
{
    CutDimensionKey key {thread, holeCutType};
    return HoleCutTypeMap.count(key)
        && HoleCutTypeMap.find(key)->second.cut_type == CutDimensionSet::Countersink;
}

/*
 * Counter Dimensions
 */

const Hole::CounterBoreDimension Hole::CounterBoreDimension::nothing {"None", 0.0, 0.0};
const Hole::CounterSinkDimension Hole::CounterSinkDimension::nothing {"None", 0.0};

void Hole::calculateAndSetCounterbore()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = (1.5 * threadDiameter) + 1.0;
    double k = threadDiameter;

    HoleCutDiameter.setValue(dk);
    HoleCutDepth.setValue(k);
}

void Hole::calculateAndSetCountersink()
{
    // estimate a reasonable value since it's not on the standard
    double threadDiameter = Diameter.getValue();
    double dk = 2.24 * threadDiameter;

    HoleCutDiameter.setValue(dk);
    ProfileBased::onChanged(&HoleCutDiameter);
}


Hole::CutDimensionKey::CutDimensionKey(const std::string& t, const std::string& c)
    : thread_type {t}
    , cut_name {c}
{}

bool Hole::CutDimensionKey::operator<(const CutDimensionKey& b) const
{
    return thread_type < b.thread_type || (thread_type == b.thread_type && cut_name < b.cut_name);
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const std::string& t, const std::string& c)
{
    return HoleCutTypeMap.find(CutDimensionKey(t, c))->second;
}

const Hole::CutDimensionSet& Hole::find_cutDimensionSet(const CutDimensionKey& k)
{
    return HoleCutTypeMap.find(k)->second;
}

Hole::CutDimensionSet::CutDimensionSet(
    const std::string& nme,
    std::vector<CounterBoreDimension>&& d,
    CutType cut,
    ThreadType thread,
    double a
)
    : bore_data {std::move(d)}
    , cut_type {cut}
    , thread_type {thread}
    , name {nme}
    , angle {a}
{}

Hole::CutDimensionSet::CutDimensionSet(
    const std::string& nme,
    std::vector<CounterSinkDimension>&& d,
    CutType cut,
    ThreadType thread,
    double a
)
    : sink_data {std::move(d)}
    , cut_type {cut}
    , thread_type {thread}
    , name {nme}
    , angle {a}
{}

const Hole::CounterBoreDimension& Hole::CutDimensionSet::get_bore(const std::string& t) const
{
    auto i = std::find_if(bore_data.begin(), bore_data.end(), [t](const Hole::CounterBoreDimension& x) {
        return x.thread == t;
    });
    if (i == bore_data.end()) {
        return CounterBoreDimension::nothing;
    }
    else {
        return *i;
    }
}

const Hole::CounterSinkDimension& Hole::CutDimensionSet::get_sink(const std::string& t) const
{
    auto i = std::find_if(sink_data.begin(), sink_data.end(), [t](const Hole::CounterSinkDimension& x) {
        return x.thread == t;
    });
    if (i == sink_data.end()) {
        return CounterSinkDimension::nothing;
    }
    else {
        return *i;
    }
}

void from_json(const nlohmann::json& j, Hole::CounterBoreDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
    t.depth = j["depth"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CounterSinkDimension& t)
{
    t.thread = j["thread"].get<std::string>();
    t.diameter = j["diameter"].get<double>();
}

void from_json(const nlohmann::json& j, Hole::CutDimensionSet& t)
{
    t.name = j["name"].get<std::string>();

    std::string thread_type_string = j["thread_type"].get<std::string>();
    if (thread_type_string == "metric") {
        t.thread_type = Hole::CutDimensionSet::Metric;
    }
    else if (thread_type_string == "metricfine") {
        t.thread_type = Hole::CutDimensionSet::MetricFine;
    }
    else {
        throw Base::IndexError(std::string("Thread type '") + thread_type_string + "' unsupported");
    }

    std::string cut_type_string = j["cut_type"].get<std::string>();
    if (cut_type_string == "counterbore") {
        t.cut_type = Hole::CutDimensionSet::Counterbore;
        t.bore_data = j["data"].get<std::vector<Hole::CounterBoreDimension>>();
        t.angle = 0.0;
    }
    else if (cut_type_string == "countersink") {
        t.cut_type = Hole::CutDimensionSet::Countersink;
        t.sink_data = j["data"].get<std::vector<Hole::CounterSinkDimension>>();
        t.angle = j["angle"].get<double>();
    }
    else {
        throw Base::IndexError(std::string("Cut type '") + cut_type_string + "' unsupported");
    }

    t.name = j["name"].get<std::string>();
}

void Hole::readCutDefinitions()
{
    std::vector<std::string> dirs {
        ::App::Application::getResourceDir() + "Mod/PartDesign/Resources/Hole",
        ::App::Application::getUserAppDataDir() + "PartDesign/Hole"
    };

    std::clog << "Looking for thread definitions in: ";
    for (auto& i : dirs) {
        std::clog << i << " ";
    }
    std::clog << "\n";
    for (auto& dir : dirs) {
        std::vector<::Base::FileInfo> files {::Base::FileInfo(dir).getDirectoryContent()};
        for (const auto& f : files) {
            if (f.extension() == "json") {
                try {
                    Base::ifstream input(f);
                    nlohmann::json j;
                    input >> j;
                    CutDimensionSet screwtype = j.get<CutDimensionSet>();
                    addCutType(screwtype);
                }
                catch (std::exception& e) {
                    std::cerr << "Failed reading '" << f.filePath() << "' with: " << e.what() << "\n";
                }
            }
        }
    }
}

int Hole::baseProfileOption_idxToBitmask(int index)
{
    // Translate combobox index to bitmask value
    // More options could be made available
    if (index == 0) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnCirclesArcs;
    }
    if (index == 1) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnPointsCirclesArcs;
    }
    if (index == 2) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnPoints;
    }
    Base::Console().error("Unexpected hole base profile combobox index: %i", index);
    return 0;
}
int Hole::baseProfileOption_bitmaskToIdx(int bitmask)
{
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnCirclesArcs) {
        return 0;
    }
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnPointsCirclesArcs) {
        return 1;
    }
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnPoints) {
        return 2;
    }

    Base::Console().error("Unexpected hole base profile bitmask: %i", bitmask);
    return -1;
}


}  // namespace PartDesign
