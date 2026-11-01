// SPDX-License-Identifier: LGPL-2.1-or-later

// Characterization tests for PartDesign::Hole. The expected values come from the current code and
// lock its behavior before the Hole moves to ThreadUtils. Change them only by an explicit decision
// that the commit message justifies.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyStandard.h>
#include <Base/FileInfo.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>
#include <Base/Tools.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Mod/PartDesign/App/FeatureHole.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "PartDesignTestHelpers.h"

using PartDesign::Hole;
using PartDesignTestHelpers::getThreadShapeSignature;
using PartDesignTestHelpers::ThreadShapeSignature;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

constexpr double boxSize = 30.0;
// Shape signatures, as in Thread.cpp.
constexpr double shapeTolerance = 1e-3;
// Volumes that have a closed formula.
constexpr double analyticTolerance = 1e-6;
// Property values.
constexpr double valueTolerance = 1e-9;

// Selects an item by its exact name. findEnumIndex() matches substrings and would take the BSW size
// "3/16" for "6".
void setEnum(App::PropertyEnumeration& property, const std::string& name)
{
    if (!property.isPartOf(name.c_str())) {
        throw std::invalid_argument("'" + name + "' is not an item of " + property.getName());
    }
    property.setValue(name.c_str());
}

void setThread(Hole& hole, const std::string& type, const std::string& size)
{
    setEnum(hole.ThreadType, type);
    setEnum(hole.ThreadSize, size);
}

// Blind flat-bottomed hole without thread or cut.
void configurePlainHole(Hole& hole)
{
    hole.Diameter.setValue(6.0);
    hole.Depth.setValue(10.0);
    setEnum(hole.DrillPoint, "Flat");
}

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

gp_Pnt centerOfMassOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.CentreOfMass();
}

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Shape> solids;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        solids.push_back(explorer.Current());
    }
    return solids;
}

// Name of the first cylindrical face of the feature's shape, or an empty string.
std::string findCylindricalFace(const Part::Feature& feature)
{
    const Part::TopoShape& shape = feature.Shape.getShape();
    for (TopExp_Explorer explorer(shape.getShape(), TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Cylinder) {
            return "Face" + std::to_string(shape.findShape(face));
        }
    }
    return {};
}

void expectSameSignature(
    const ThreadShapeSignature& actual,
    const ThreadShapeSignature& expected,
    double tolerance = shapeTolerance
)
{
    EXPECT_NEAR(actual.volume, expected.volume, tolerance);
    EXPECT_NEAR(actual.surface, expected.surface, tolerance);
    EXPECT_NEAR(actual.centerOfMass.X(), expected.centerOfMass.X(), tolerance);
    EXPECT_NEAR(actual.centerOfMass.Y(), expected.centerOfMass.Y(), tolerance);
    EXPECT_NEAR(actual.centerOfMass.Z(), expected.centerOfMass.Z(), tolerance);
    EXPECT_NEAR(actual.xmin, expected.xmin, tolerance);
    EXPECT_NEAR(actual.ymin, expected.ymin, tolerance);
    EXPECT_NEAR(actual.zmin, expected.zmin, tolerance);
    EXPECT_NEAR(actual.xmax, expected.xmax, tolerance);
    EXPECT_NEAR(actual.ymax, expected.ymax, tolerance);
    EXPECT_NEAR(actual.zmax, expected.zmax, tolerance);
    EXPECT_EQ(actual.faces, expected.faces);
}

// Values of the properties in the "Hole" group: numbers for float properties, text for the others.
struct PropertySnapshot
{
    std::map<std::string, std::string> texts;
    std::map<std::string, double> numbers;
};

PropertySnapshot snapshotOf(const Hole& hole)
{
    PropertySnapshot snapshot;
    std::vector<App::Property*> properties;
    hole.getPropertyList(properties);
    for (const App::Property* property : properties) {
        const char* group = hole.getPropertyGroup(property);
        if (!group || std::string(group) != "Hole") {
            continue;
        }
        const std::string name = property->getName();
        if (const auto* number = dynamic_cast<const App::PropertyFloat*>(property)) {
            snapshot.numbers[name] = number->getValue();
        }
        else if (const auto* enumeration = dynamic_cast<const App::PropertyEnumeration*>(property)) {
            snapshot.texts[name] = enumeration->getValueAsString();
        }
        else if (const auto* flag = dynamic_cast<const App::PropertyBool*>(property)) {
            snapshot.texts[name] = flag->getValue() ? "true" : "false";
        }
        else if (const auto* integer = dynamic_cast<const App::PropertyInteger*>(property)) {
            snapshot.texts[name] = std::to_string(integer->getValue());
        }
        else {
            snapshot.texts[name] = "unsupported " + std::string(property->getTypeId().getName());
        }
    }
    return snapshot;
}

void expectSameProperties(const PropertySnapshot& actual, const PropertySnapshot& expected)
{
    EXPECT_EQ(actual.texts, expected.texts);
    EXPECT_EQ(actual.numbers.size(), expected.numbers.size());
    for (const auto& [name, value] : expected.numbers) {
        const auto found = actual.numbers.find(name);
        if (found == actual.numbers.end()) {
            ADD_FAILURE() << name << " is missing";
            continue;
        }
        EXPECT_NEAR(found->second, value, valueTolerance) << name;
    }
}

// Sets an integer preference of Mod/PartDesign and restores the previous state on destruction.
class ScopedPartDesignPreference
{
public:
    explicit ScopedPartDesignPreference(const char* name)
        : _name(name)
        , _group(
              App::GetApplication()
                  .GetUserParameter()
                  .GetGroup("BaseApp")
                  ->GetGroup("Preferences")
                  ->GetGroup("Mod/PartDesign")
          )
        , _saved(_group->GetInt(name, unset))
    {}

    ScopedPartDesignPreference(const ScopedPartDesignPreference&) = delete;
    ScopedPartDesignPreference& operator=(const ScopedPartDesignPreference&) = delete;

    ~ScopedPartDesignPreference()
    {
        if (_saved == unset) {
            _group->RemoveInt(_name.c_str());
        }
        else {
            _group->SetInt(_name.c_str(), _saved);
        }
    }

    void set(long value)
    {
        _group->SetInt(_name.c_str(), value);
    }

private:
    static constexpr long unset = std::numeric_limits<long>::min();
    std::string _name;
    Base::Reference<ParameterGrp> _group;
    long _saved;
};

}  // namespace

class HoleTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Hole_test", "testUser");
        _body = _doc->addObject<PartDesign::Body>();
        _base = _doc->addObject<PartDesign::Feature>("Base");
        _body->addObject(_base);
        _base->Shape.setValue(
            BRepPrimAPI_MakeBox(gp_Pnt(-boxSize / 2.0, -boxSize / 2.0, 0.0), boxSize, boxSize, boxSize)
                .Shape()
        );
        _sketch = addSketch("Sketch", true);
        addCircle(_sketch, Base::Vector3d(0.0, 0.0, 0.0), 1.0);
        _doc->recompute();
        _hole = addHole("Hole");
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
        }
        if (!_savedFile.empty()) {
            Base::FileInfo(_savedFile).deleteFile();
        }
    }

    App::Document* getDocument() const
    {
        return _doc;
    }

    PartDesign::Feature* getBase() const
    {
        return _base;
    }

    Hole* getHole() const
    {
        return _hole;
    }

    // Sketch on XY_Plane. With mapReversed its normal points to -Z, so a hole drills into the box
    // (+Z) without Reversed.
    Sketcher::SketchObject* addSketch(const char* name, bool mapReversed)
    {
        auto* sketch = _doc->addObject<Sketcher::SketchObject>(name);
        _body->addObject(sketch);
        sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        sketch->MapMode.setValue("FlatFace");
        sketch->MapReversed.setValue(mapReversed);
        return sketch;
    }

    static void addCircle(Sketcher::SketchObject* sketch, const Base::Vector3d& center, double radius)
    {
        Part::GeomCircle circle;
        circle.setLocation(center);
        circle.setRadius(radius);
        sketch->addGeometry(&circle, false);
    }

    // Hole on the box from the fixture sketch. Refine is fixed because its default comes from the
    // user preferences.
    Hole* addHole(const char* name)
    {
        auto* hole = _doc->addObject<Hole>(name);
        _body->addObject(hole);
        hole->Profile.setValue(_sketch);
        hole->BaseFeature.setValue(_base);
        hole->Refine.setValue(false);
        return hole;
    }

    void removeHole(Hole* hole)
    {
        _body->removeObject(hole);
        _doc->removeObject(hole->getNameInDocument());
    }

    // Replaces the fixture hole, so that every case starts from the same state.
    Hole* newHole()
    {
        removeHole(_hole);
        _hole = addHole("Hole");
        return _hole;
    }

    // Recomputes the document. Records a failure and returns false when the hole has an error.
    bool recomputeHole(const Hole& hole)
    {
        _doc->recompute();
        EXPECT_FALSE(hole.isError()) << hole.getStatusString();
        return !hole.isError();
    }

    // Saves the document, closes it and opens the file again. Returns the reopened fixture hole.
    Hole* reopenDocument()
    {
        const std::string holeName = _hole->getNameInDocument();
        _savedFile = App::Application::getTempPath() + "HoleTest_persistence.FCStd";
        if (!_doc->saveAs(_savedFile.c_str())) {
            return nullptr;
        }
        App::GetApplication().closeDocument(_doc->getName());
        _doc = App::GetApplication().openDocument(_savedFile.c_str());
        if (!_doc) {
            return nullptr;
        }
        _body = dynamic_cast<PartDesign::Body*>(_doc->getObject("Body"));
        _base = dynamic_cast<PartDesign::Feature*>(_doc->getObject("Base"));
        _sketch = dynamic_cast<Sketcher::SketchObject*>(_doc->getObject("Sketch"));
        _hole = dynamic_cast<Hole*>(_doc->getObject(holeName.c_str()));
        return _hole;
    }

private:
    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    PartDesign::Feature* _base = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    Hole* _hole = nullptr;
    std::string _savedFile;
};

TEST_F(HoleTest, GeometryVariants)
{
    constexpr double radius = 3.0;
    constexpr double depth = 10.0;
    // The angled point is a cone whose half angle is DrillPointAngle / 2 from the axis.
    const double tipHeight = radius * std::tan(Base::toRadians((180.0 - 118.0) / 2.0));

    // Compares the hole with a reference hole that configureReference builds.
    const auto expectSameShapesAs = [this](std::function<void(Hole&, const Hole&)> configureReference) {
        return [this, configureReference](const Hole& hole) {
            auto* reference = addHole("Reference");
            configureReference(*reference, hole);
            if (recomputeHole(*reference)) {
                expectSameSignature(
                    getThreadShapeSignature(hole.AddSubShape.getValue()),
                    getThreadShapeSignature(reference->AddSubShape.getValue())
                );
                expectSameSignature(
                    getThreadShapeSignature(hole.Shape.getValue()),
                    getThreadShapeSignature(reference->Shape.getValue())
                );
            }
            removeHole(reference);
        };
    };

    struct GeometryCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        std::function<void(const Hole&)> verify;
    };
    const std::vector<GeometryCase> cases {
        {
            "angled drill point without DrillForDepth",
            [](Hole& hole) {
                configurePlainHole(hole);
                setEnum(hole.DrillPoint, "Angled");
                hole.DrillPointAngle.setValue(118.0);
                hole.DrillForDepth.setValue(false);
            },
            [&](const Hole& hole) {
                // Cylinder plus the cone of the point.
                const double expected = std::numbers::pi * radius * radius
                    * (depth + tipHeight / 3.0);
                EXPECT_NEAR(volumeOf(hole.AddSubShape.getValue()), expected, analyticTolerance);
            },
        },
        {
            "through all",
            [](Hole& hole) {
                configurePlainHole(hole);
                setEnum(hole.DepthType, "ThroughAll");
            },
            [&](const Hole& hole) {
                const double expected = boxSize * boxSize * boxSize
                    - std::numbers::pi * radius * radius * boxSize;
                EXPECT_NEAR(volumeOf(hole.Shape.getValue()), expected, analyticTolerance);
            },
        },
        {
            "reversed on a sketch without MapReversed",
            [this](Hole& hole) {
                auto* sketch = addSketch("PlainSketch", false);
                addCircle(sketch, Base::Vector3d(0.0, 0.0, 0.0), 1.0);
                hole.Profile.setValue(sketch);
                configurePlainHole(hole);
                hole.Reversed.setValue(true);
            },
            expectSameShapesAs([](Hole& reference, const Hole&) { configurePlainHole(reference); }),
        },
        {
            "counterdrill",
            [](Hole& hole) {
                configurePlainHole(hole);
                setEnum(hole.HoleCutType, "Counterdrill");
            },
            [](const Hole& hole) {
                expectSameSignature(
                    getThreadShapeSignature(hole.AddSubShape.getValue()),
                    {387.443197189268,
                     347.666778505486,
                     gp_Pnt(0.0, 0.0, 3.93834014449391),
                     -5.1000001,
                     -5.1000001,
                     -1e-07,
                     5.1000001,
                     5.1000001,
                     10.0000001,
                     5}
                );
            },
        },
        {
            "cosmetic thread",
            [](Hole& hole) {
                setThread(hole, "ISOMetricProfile", "M6x1.0");
                hole.Threaded.setValue(true);
                hole.ModelThread.setValue(false);
                hole.Depth.setValue(depth);
                setEnum(hole.DrillPoint, "Flat");
            },
            // Same as a plain hole with the tap drill diameter.
            expectSameShapesAs([](Hole& reference, const Hole& hole) {
                configurePlainHole(reference);
                reference.Diameter.setValue(hole.Diameter.getValue());
            }),
        },
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.configure(*hole);
        if (recomputeHole(*hole)) {
            testCase.verify(*hole);
        }
    }
}

TEST_F(HoleTest, HoleCutParameters)
{
    struct HoleCutValues
    {
        double diameter;
        double depth;
        double countersinkAngle;
        bool customValues;
    };
    // Depth of a countersink of the given diameter and angle in degrees.
    const auto countersinkDepth = [](double diameter, double angle) {
        return (diameter / 2.0) / std::tan(Base::toRadians(angle) / 2.0);
    };
    // Counterbore estimated when no standard defines the size.
    const auto estimatedCounterbore = [](const Hole& hole, bool customValues) {
        const double diameter = hole.Diameter.getValue();
        return HoleCutValues {1.5 * diameter + 1.0, diameter, 90.0, customValues};
    };
    // Countersink rule of thumb for types without screw definitions.
    const auto countersinkRule = [countersinkDepth](const Hole& hole, double angle) {
        const double diameter = 1.7 * hole.Diameter.getValue();
        return HoleCutValues {diameter, countersinkDepth(diameter, angle), angle, false};
    };
    const auto withCut = [](const std::string& type, const std::string& size, const std::string& cut) {
        return [type, size, cut](Hole& hole) {
            setThread(hole, type, size);
            setEnum(hole.HoleCutType, cut);
        };
    };
    // ISO 4762 from iso4762.json with custom values: 12 x 7 instead of 11 x 6.4.
    const auto customIso4762 = [](Hole& hole) {
        setThread(hole, "ISOMetricProfile", "M6x1.0");
        setEnum(hole.HoleCutType, "ISO 4762");
        hole.HoleCutCustomValues.setValue(true);
        hole.HoleCutDiameter.setValue(12.0);
        hole.HoleCutDepth.setValue(7.0);
    };

    struct HoleCutCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        std::function<HoleCutValues(const Hole&)> expected;
    };
    const std::vector<HoleCutCase> cases {
        // The JSON lookup uses the ThreadSize text, here "M6x1.0".
        {
            "ISO M6 counterbore from ISO 4762",
            withCut("ISOMetricProfile", "M6x1.0", "Counterbore"),
            [](const Hole&) { return HoleCutValues {11.0, 6.4, 90.0, false}; },
        },
        {
            "ISO M6 countersink from ISO 10642",
            withCut("ISOMetricProfile", "M6x1.0", "Countersink"),
            [&](const Hole&) {
                return HoleCutValues {13.4, countersinkDepth(13.4, 90.0), 90.0, false};
            },
        },
        {
            "ISO M6 counterdrill",
            withCut("ISOMetricProfile", "M6x1.0", "Counterdrill"),
            [](const Hole&) { return HoleCutValues {13.4, 1.0, 90.0, false}; },
        },
        {
            "ISO M6 screw type with custom values",
            customIso4762,
            [](const Hole&) { return HoleCutValues {12.0, 7.0, 90.0, true}; },
        },
        {
            "ISO M6 screw type back to standard values",
            [&](Hole& hole) {
                customIso4762(hole);
                hole.HoleCutCustomValues.setValue(false);
            },
            [](const Hole&) { return HoleCutValues {11.0, 6.4, 90.0, false}; },
        },
        // M1x0.25 is not in iso4762.json.
        {
            "ISO M1 counterbore without ISO 4762 size",
            withCut("ISOMetricProfile", "M1x0.25", "Counterbore"),
            [&](const Hole& hole) { return estimatedCounterbore(hole, false); },
        },
        {
            "ISO M1 screw type without ISO 4762 size forces custom values",
            withCut("ISOMetricProfile", "M1x0.25", "ISO 4762"),
            [&](const Hole& hole) { return estimatedCounterbore(hole, true); },
        },
        {
            "UNC 1/4 counterbore",
            withCut("UNC", "1/4", "Counterbore"),
            [](const Hole& hole) {
                const double diameter = hole.Diameter.getValue();
                return HoleCutValues {1.6 * diameter, 0.9 * diameter, 90.0, false};
            },
        },
        {
            "UNC 1/4 countersink",
            withCut("UNC", "1/4", "Countersink"),
            [&](const Hole& hole) { return countersinkRule(hole, 82.0); },
        },
        {
            "BSW 1/4 countersink",
            withCut("BSW", "1/4", "Countersink"),
            [&](const Hole& hole) { return countersinkRule(hole, 100.0); },
        },
        {
            "BSP 1/4 countersink",
            withCut("BSP", "1/4", "Countersink"),
            [&](const Hole& hole) { return countersinkRule(hole, 90.0); },
        },
        {
            "diameter beyond the counterbore recalculates the cut",
            [](Hole& hole) {
                setEnum(hole.HoleCutType, "Counterbore");
                hole.Diameter.setValue(10.0);
            },
            [](const Hole&) { return HoleCutValues {1.6 * 10.0, 0.9 * 10.0, 90.0, false}; },
        },
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.configure(*hole);
        const HoleCutValues expected = testCase.expected(*hole);
        EXPECT_NEAR(hole->HoleCutDiameter.getValue(), expected.diameter, valueTolerance);
        EXPECT_NEAR(hole->HoleCutDepth.getValue(), expected.depth, valueTolerance);
        EXPECT_NEAR(hole->HoleCutCountersinkAngle.getValue(), expected.countersinkAngle, valueTolerance);
        EXPECT_EQ(hole->HoleCutCustomValues.getValue(), expected.customValues);
    }
}

TEST_F(HoleTest, HolePlacementFromProfile)
{
    constexpr double depth = 5.0;
    const gp_Dir intoBox(0.0, 0.0, 1.0);

    // Circle, arc and standalone point on a sketch that drills into the box.
    auto* mixed = addSketch("MixedSketch", true);
    addCircle(mixed, Base::Vector3d(-8.0, -8.0, 0.0), 2.0);
    Part::GeomArcOfCircle arc;
    arc.setLocation(Base::Vector3d(8.0, -8.0, 0.0));
    arc.setRadius(2.0);
    arc.setRange(0.0, std::numbers::pi, true);
    mixed->addGeometry(&arc, false);
    const Part::GeomPoint point(Base::Vector3d(0.0, 8.0, 0.0));
    mixed->addGeometry(&point, false);

    // Blind hole whose cylindrical face is the profile of another hole.
    auto* existing = getHole();
    configurePlainHole(*existing);
    ASSERT_TRUE(recomputeHole(*existing));
    const std::string cylindricalFace = findCylindricalFace(*existing);
    ASSERT_FALSE(cylindricalFace.empty());

    const auto onSketch = [mixed](double x, double y) {
        Base::Vector3d position(x, y, 0.0);
        mixed->Placement.getValue().multVec(position, position);
        return gp_Pnt(position.x, position.y, position.z);
    };
    const gp_Pnt circleCenter = onSketch(-8.0, -8.0);
    const gp_Pnt arcCenter = onSketch(8.0, -8.0);
    const gp_Pnt standalonePoint = onSketch(0.0, 8.0);

    const auto onMixedSketch = [mixed](Hole& hole) {
        hole.Profile.setValue(mixed);
    };
    const auto onCylindricalFace = [existing, cylindricalFace](Hole& hole) {
        hole.Profile.setValue(existing, {cylindricalFace});
        hole.BaseFeature.setValue(existing);
    };

    struct PlacementCase
    {
        std::string name;
        std::function<void(Hole&)> setProfile;
        int baseProfileType;
        std::vector<gp_Pnt> locations;
        gp_Dir drillDirection;
    };
    const std::vector<PlacementCase> cases {
        {"circles and arcs", onMixedSketch, Hole::OnCirclesArcs, {circleCenter, arcCenter}, intoBox},
        {"points", onMixedSketch, Hole::OnPoints, {standalonePoint}, intoBox},
        {
            "points, circles and arcs",
            onMixedSketch,
            Hole::OnPointsCirclesArcs,
            {circleCenter, arcCenter, standalonePoint},
            intoBox,
        },
        // One hole per circular edge of the face. guessNormalDirection() takes the cylinder axis,
        // +Z here, as the profile normal, so both holes drill toward -Z: the one at the bottom of
        // the existing hole (z = 10) goes back into it, the one at its entry (z = 0) leaves the box.
        {
            "cylindrical face of a hole",
            onCylindricalFace,
            Hole::OnCirclesArcs,
            {gp_Pnt(0.0, 0.0, 10.0), gp_Pnt(0.0, 0.0, 0.0)},
            gp_Dir(0.0, 0.0, -1.0),
        },
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = addHole("Placed");
        testCase.setProfile(*hole);
        hole->BaseProfileType.setValue(testCase.baseProfileType);
        hole->Diameter.setValue(8.0);
        hole->Depth.setValue(depth);
        setEnum(hole->DrillPoint, "Flat");
        if (recomputeHole(*hole)) {
            const std::vector<gp_Pnt> locations = hole->getHoleLocations();
            const std::vector<TopoDS_Shape> solids = solidsOf(hole->AddSubShape.getValue());
            EXPECT_EQ(locations.size(), testCase.locations.size());
            EXPECT_EQ(solids.size(), testCase.locations.size());
            const size_t count = std::min({locations.size(), solids.size(), testCase.locations.size()});
            for (size_t i = 0; i < count; ++i) {
                SCOPED_TRACE("hole " + std::to_string(i));
                EXPECT_NEAR(locations[i].Distance(testCase.locations[i]), 0.0, valueTolerance);
                // A flat-bottomed hole is a cylinder: its center of mass is on the axis, half the
                // depth into the material.
                const gp_Pnt center = testCase.locations[i].Translated(
                    gp_Vec(testCase.drillDirection) * (depth / 2.0)
                );
                EXPECT_NEAR(centerOfMassOf(solids[i]).Distance(center), 0.0, analyticTolerance);
            }
        }
        removeHole(hole);
    }

    // Index of the base profile combo box and the matching bitmask.
    const std::vector<std::pair<int, int>> options {
        {0, Hole::OnCirclesArcs},
        {1, Hole::OnPointsCirclesArcs},
        {2, Hole::OnPoints},
    };
    for (const auto& [index, bitmask] : options) {
        SCOPED_TRACE("option " + std::to_string(index));
        EXPECT_EQ(Hole::baseProfileOption_idxToBitmask(index), bitmask);
        EXPECT_EQ(Hole::baseProfileOption_bitmaskToIdx(bitmask), index);
    }
    EXPECT_EQ(Hole::baseProfileOption_idxToBitmask(3), 0);
    EXPECT_EQ(Hole::baseProfileOption_bitmaskToIdx(Hole::OnCircles), -1);

    // New objects take the type from the preferences.
    {
        ScopedPartDesignPreference preference("defaultBaseTypeHole");
        for (const auto& [index, bitmask] : options) {
            SCOPED_TRACE("preference " + std::to_string(index));
            preference.set(index);
            auto* hole = addHole("FromPreference");
            EXPECT_EQ(hole->BaseProfileType.getValue(), bitmask);
            removeHole(hole);
        }
    }

    // The constructor default is what files saved before BaseProfileType get.
    const Hole detached;
    EXPECT_EQ(detached.BaseProfileType.getValue(), Hole::OnCirclesArcs);
}

TEST_F(HoleTest, InvalidInputsFailAndRecover)
{
    struct InvalidCase
    {
        std::string name;
        std::function<void(Hole&)> breakHole;
        std::string message;
        std::function<void(Hole&)> fix;
    };
    const std::vector<InvalidCase> cases {
        {
            "diameter below the minimum",
            // In C++, setValue() does not apply the constraint that setPyObject() applies.
            [](Hole& hole) {
                configurePlainHole(hole);
                hole.Diameter.setValue(0.0);
            },
            "Diameter too small",
            [](Hole& hole) { hole.Diameter.setValue(6.0); },
        },
        {
            "hole cut diameter smaller than the diameter",
            [](Hole& hole) {
                configurePlainHole(hole);
                setEnum(hole.HoleCutType, "Counterbore");
                hole.HoleCutDiameter.setValue(4.0);
            },
            "Hole cut diameter too small",
            [](Hole& hole) { hole.HoleCutDiameter.setValue(9.6); },
        },
        {
            "hole cut deeper than the hole",
            [](Hole& hole) {
                configurePlainHole(hole);
                setEnum(hole.HoleCutType, "Counterbore");
                hole.HoleCutDepth.setValue(12.0);
            },
            "Hole cut depth must be less than hole depth",
            [](Hole& hole) { hole.HoleCutDepth.setValue(5.0); },
        },
        {
            "countersink beyond the bottom",
            // The 10.2 mm countersink meets the 6 mm wall 2.1 mm deep.
            [](Hole& hole) {
                configurePlainHole(hole);
                hole.Depth.setValue(2.0);
                setEnum(hole.HoleCutType, "Countersink");
            },
            "Invalid countersink",
            [](Hole& hole) { hole.Depth.setValue(10.0); },
        },
        {
            "no base feature",
            [](Hole& hole) {
                configurePlainHole(hole);
                hole.BaseFeature.setValue(nullptr);
            },
            "does not contain a base shape",
            [this](Hole& hole) { hole.BaseFeature.setValue(getBase()); },
        },
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.breakHole(*hole);
        getDocument()->recompute();
        EXPECT_TRUE(hole->isError());
        EXPECT_NE(std::string(hole->getStatusString()).find(testCase.message), std::string::npos)
            << hole->getStatusString();

        testCase.fix(*hole);
        recomputeHole(*hole);
    }
}

TEST_F(HoleTest, DerivedThreadParameters)
{
    const auto tapped = [](const std::string& type, const std::string& size) {
        return [type, size](Hole& hole) {
            setThread(hole, type, size);
            hole.Threaded.setValue(true);
        };
    };
    // The class goes before ModelThread: Hole::onChanged() has no ThreadClass branch, so a later
    // class change would leave Diameter with the clearance of the previous class.
    const auto modeledM6 = [](const std::function<void(Hole&)>& clearance) {
        return [clearance](Hole& hole) {
            setThread(hole, "ISOMetricProfile", "M6x1.0");
            hole.Threaded.setValue(true);
            clearance(hole);
            hole.ModelThread.setValue(true);
        };
    };
    const auto clearanceHole =
        [](const std::string& type, const std::string& size, const std::string& fit) {
            return [type, size, fit](Hole& hole) {
                setThread(hole, type, size);
                setEnum(hole.ThreadFit, fit);
            };
        };

    struct DiameterCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        double diameter;
        double threadDiameter;
    };
    const std::vector<DiameterCase> diameterCases {
        // Threaded: tap drill from the table, or a formula when the table has none.
        {"ISO M6 tap drill", tapped("ISOMetricProfile", "M6x1.0"), 5.0, 6.0},
        {
            "BSP 1 1/8 without tap drill",
            tapped("BSP", "1 1/8"),
            37.897 - 2.0 * 0.640327 * 2.309 * 0.75,
            37.897,
        },
        {"NPT 1/8 without tap drill", tapped("NPT", "1/8"), 10.287 - 2.0 * 0.8 * 0.941 * 0.75, 10.287},
        {"ISOTyre 8v1 without tap drill", tapped("ISOTyre", "8v1"), 7.798 - 0.794, 7.798},
        // A modeled thread adds the clearance of the class or the custom one.
        {
            "ISO M6 modeled 6H",
            modeledM6([](Hole& hole) { setEnum(hole.ThreadClass, "6H"); }),
            5.0,
            6.0,
        },
        {
            "ISO M6 modeled 6G",
            modeledM6([](Hole& hole) { setEnum(hole.ThreadClass, "6G"); }),
            5.0 + 0.026,
            6.0,
        },
        {
            "ISO M6 modeled with custom clearance",
            modeledM6([](Hole& hole) {
                hole.UseCustomThreadClearance.setValue(true);
                hole.CustomThreadClearance.setValue(0.1);
            }),
            5.0 + 0.1,
            6.0,
        },
        // Clearance holes: ISO 273, ASME B18.2.8, or a factor where these have no value.
        {"ISO M6 medium fit", clearanceHole("ISOMetricProfile", "M6x1.0", "Medium"), 6.6, 6.0},
        {"ISO M6 fine fit", clearanceHole("ISOMetricProfile", "M6x1.0", "Fine"), 6.4, 6.0},
        {"ISO M6 coarse fit", clearanceHole("ISOMetricProfile", "M6x1.0", "Coarse"), 7.0, 6.0},
        {"ISO M9 medium fit", clearanceHole("ISOMetricProfile", "M9x1.25", "Medium"), 9.0 * 1.10, 9.0},
        {"UNC 1/4 normal fit", clearanceHole("UNC", "1/4", "Normal"), 7.1, 6.35},
        {"UNC 9/16 normal fit", clearanceHole("UNC", "9/16", "Normal"), 14.288 * 1.08, 14.288},
        {"BSW 1/4 normal fit", clearanceHole("BSW", "1/4", "Normal"), 6.35 * 1.1, 6.35},
        {"BSW 1/4 close fit", clearanceHole("BSW", "1/4", "Close"), 6.35 * 1.05, 6.35},
        {"BSW 1/4 wide fit", clearanceHole("BSW", "1/4", "Wide"), 6.35 * 1.15, 6.35},
    };
    for (const auto& testCase : diameterCases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.configure(*hole);
        EXPECT_NEAR(hole->Diameter.getValue(), testCase.diameter, valueTolerance);
        EXPECT_NEAR(hole->ThreadDiameter.getValue(), testCase.threadDiameter, valueTolerance);
    }

    struct ThreadFlags
    {
        bool threaded;
        bool modelThread;
        bool cosmeticThread;
        bool useCustomThreadClearance;
        double taperedAngle;
    };
    struct RuleCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        ThreadFlags expected;
    };
    const auto tappedM6 = tapped("ISOMetricProfile", "M6x1.0");
    const auto tappedNpt = tapped("NPT", "1/8");
    const std::vector<RuleCase> ruleCases {
        {
            "ModelThread turns CosmeticThread off",
            [&](Hole& hole) {
                tappedM6(hole);
                hole.CosmeticThread.setValue(true);
                hole.ModelThread.setValue(true);
            },
            {true, true, false, false, 90.0},
        },
        {
            "CosmeticThread turns ModelThread off",
            [&](Hole& hole) {
                tappedM6(hole);
                hole.ModelThread.setValue(true);
                hole.CosmeticThread.setValue(true);
            },
            {true, false, true, false, 90.0},
        },
        {
            "threaded taper takes the pipe thread angle",
            [&](Hole& hole) {
                tappedNpt(hole);
                hole.Tapered.setValue(true);
            },
            {true, false, true, false, 90.0 - 1.79},
        },
        {
            "taper off resets the angle",
            [&](Hole& hole) {
                tappedNpt(hole);
                hole.Tapered.setValue(true);
                hole.Tapered.setValue(false);
            },
            {true, false, true, false, 90.0},
        },
        {
            "type None clears the thread flags",
            [&](Hole& hole) {
                tappedM6(hole);
                hole.ModelThread.setValue(true);
                hole.UseCustomThreadClearance.setValue(true);
                setEnum(hole.ThreadType, "None");
            },
            {false, false, false, false, 90.0},
        },
    };
    for (const auto& testCase : ruleCases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.configure(*hole);
        EXPECT_EQ(hole->Threaded.getValue(), testCase.expected.threaded);
        EXPECT_EQ(hole->ModelThread.getValue(), testCase.expected.modelThread);
        EXPECT_EQ(hole->CosmeticThread.getValue(), testCase.expected.cosmeticThread);
        EXPECT_EQ(hole->UseCustomThreadClearance.getValue(), testCase.expected.useCustomThreadClearance);
        EXPECT_NEAR(hole->TaperedAngle.getValue(), testCase.expected.taperedAngle, valueTolerance);
    }

    // Fingerprint of the whole thread table: number of sizes and sums of the pitch, of the
    // threaded Diameter and of ThreadDiameter. ThreadDiameter starts at the second size because
    // the first one keeps the value of the previous selection.
    struct TableFingerprint
    {
        std::string threadType;
        size_t sizes;
        double pitchSum;
        double diameterSum;
        double threadDiameterSum;
    };
    // Sizes and sums for 416 of the 417 rows of Hole::threadDescription; the other row is the "---"
    // of None.
    const std::vector<TableFingerprint> fingerprints {
        {"ISOMetricProfile", 40, 90.15, 721.65, 810.8},
        {"ISOMetricFineProfile", 172, 377.0, 7349.8, 7728.3},
        {"UNC", 33, 103.894, 937.3, 1036.766},
        {"UNF", 25, 29.629, 311.23, 338.533},
        {"UNEF", 24, 28.624, 541.45, 560.389},
        {"NPT", 19, 45.743, 1729.7254, 1776.679},
        {"BSP", 24, 48.688, 1475.2435017905, 1517.063},
        {"BSW", 30, 145.065, 1388.0184855075, 1528.819},
        {"BSF", 30, 89.827, 1102.486210453, 1187.452},
        {"ISOTyre", 19, 19.602, 214.336, 228.604},
    };
    for (const auto& expected : fingerprints) {
        SCOPED_TRACE(expected.threadType);
        auto* hole = newHole();
        setEnum(hole->ThreadType, expected.threadType);
        hole->Threaded.setValue(true);
        const size_t sizes = hole->ThreadSize.getEnumVector().size();
        double pitchSum = 0.0;
        double diameterSum = 0.0;
        double threadDiameterSum = 0.0;
        for (size_t size = 0; size < sizes; ++size) {
            hole->ThreadSize.setValue(static_cast<long>(size));
            pitchSum += hole->getThreadPitch();
            diameterSum += hole->Diameter.getValue();
            if (size > 0) {
                threadDiameterSum += hole->ThreadDiameter.getValue();
            }
        }
        EXPECT_EQ(sizes, expected.sizes);
        EXPECT_NEAR(pitchSum, expected.pitchSum, valueTolerance);
        EXPECT_NEAR(diameterSum, expected.diameterSum, valueTolerance);
        EXPECT_NEAR(threadDiameterSum, expected.threadDiameterSum, valueTolerance);
    }
}

TEST_F(HoleTest, ThreadDepthRules)
{
    const double throughAll = getHole()->getThroughAllLength();
    // DIN 76 runout for the 1 mm pitch of M6.
    constexpr double runoutM6 = 5.1;

    struct DepthCase
    {
        std::string name;
        std::string threadType;
        std::string threadSize;
        std::string depthType;
        std::string threadDepthType;
        double depth;
        std::optional<double> threadDepth;
        double expectedDepth;
        double expectedThreadDepth;
    };
    const std::string iso = "ISOMetricProfile";
    const std::string m6 = "M6x1.0";
    const std::vector<DepthCase> cases {
        {"dimension, hole depth", iso, m6, "Dimension", "Hole Depth", 20.0, std::nullopt, 20.0, 20.0},
        {"dimension, dimension", iso, m6, "Dimension", "Dimension", 20.0, 12.0, 20.0, 12.0},
        {
            "dimension, tapped",
            iso,
            m6,
            "Dimension",
            "Tapped (DIN76)",
            20.0,
            std::nullopt,
            20.0,
            20.0 - runoutM6,
        },
        {
            "through all, hole depth",
            iso,
            m6,
            "ThroughAll",
            "Hole Depth",
            20.0,
            std::nullopt,
            throughAll,
            throughAll,
        },
        {"through all, dimension", iso, m6, "ThroughAll", "Dimension", 20.0, 12.0, throughAll, 12.0},
        {
            "through all, tapped",
            iso,
            m6,
            "ThroughAll",
            "Tapped (DIN76)",
            20.0,
            std::nullopt,
            throughAll,
            throughAll,
        },
        // The 10.16 mm pitch is beyond the DIN 76 table, so the runout is 4 pitches.
        {
            "tapped with a pitch beyond DIN 76",
            "BSW",
            "6",
            "Dimension",
            "Tapped (DIN76)",
            50.0,
            std::nullopt,
            50.0,
            50.0 - 4.0 * 10.16,
        },
        {
            "depth beyond the through-all length",
            iso,
            m6,
            "Dimension",
            "Hole Depth",
            200.0,
            std::nullopt,
            throughAll,
            throughAll,
        },
        {"thread deeper than the hole", iso, m6, "Dimension", "Dimension", 10.0, 15.0, 10.0, 10.0},
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        setThread(*hole, testCase.threadType, testCase.threadSize);
        hole->Threaded.setValue(true);
        hole->Depth.setValue(testCase.depth);
        setEnum(hole->DepthType, testCase.depthType);
        setEnum(hole->ThreadDepthType, testCase.threadDepthType);
        if (testCase.threadDepth) {
            hole->ThreadDepth.setValue(*testCase.threadDepth);
        }
        EXPECT_NEAR(hole->Depth.getValue(), testCase.expectedDepth, valueTolerance);
        EXPECT_NEAR(hole->ThreadDepth.getValue(), testCase.expectedThreadDepth, valueTolerance);
    }
}

TEST_F(HoleTest, ThreadEnumsAndPersistence)
{
    using Items = std::vector<std::string>;
    using Names = std::set<std::string>;
    const Items isoClasses {"4G", "4H", "5G", "5H", "6G", "6H", "7G", "7H", "8G", "8H"};
    const Items utsClasses {"1B", "2B", "3B"};
    const Items whitworthClasses {"Medium", "Normal"};
    const Items noClass {"None"};
    const Items metricFits {"Medium", "Fine", "Coarse"};
    const Items utsFits {"Normal", "Close", "Loose"};
    const Items otherFits {"Normal", "Close", "Wide"};
    const Items noFit {"-", "-", "-"};
    const Items cutTypes {"None", "Counterbore", "Countersink", "Counterdrill"};
    // Screw types from Resources/Hole/*.json, in the order of the directory listing.
    const Names metricScrews {
        "DIN 7984",
        "ISO 10642",
        "ISO 14583",
        "ISO 14583 (partial)",
        "ISO 2009",
        "ISO 4762",
        "ISO 4762 + 7089",
        "ISO 7046",
    };
    const Names metricFineScrews {"ISO 10642", "ISO 12474", "ISO 4762"};

    struct TypeListsCase
    {
        std::string threadType;
        Items threadClass;
        Items threadFit;
        Names screwTypes;
    };
    // ISOTyre keeps the ThreadFit list of the previous type. Every case starts from a new hole,
    // so ISOTyre gets the metric list of the constructor.
    const std::vector<TypeListsCase> typeCases {
        {"None", noClass, noFit, {}},
        {"ISOMetricProfile", isoClasses, metricFits, metricScrews},
        {"ISOMetricFineProfile", isoClasses, metricFits, metricFineScrews},
        {"UNC", utsClasses, utsFits, {}},
        {"UNF", utsClasses, utsFits, {}},
        {"UNEF", utsClasses, utsFits, {}},
        {"NPT", noClass, utsFits, {}},
        {"BSP", noClass, metricFits, {}},
        {"BSW", whitworthClasses, otherFits, {}},
        {"BSF", whitworthClasses, otherFits, {}},
        {"ISOTyre", noClass, metricFits, {}},
    };
    for (const auto& testCase : typeCases) {
        SCOPED_TRACE(testCase.threadType);
        auto* hole = newHole();
        setEnum(hole->ThreadType, testCase.threadType);
        EXPECT_EQ(hole->ThreadClass.getEnumVector(), testCase.threadClass);
        EXPECT_EQ(hole->ThreadFit.getEnumVector(), testCase.threadFit);
        // Files store the index, so the fixed cut types must keep their order.
        const Items holeCutTypes = hole->HoleCutType.getEnumVector();
        const auto fixedEnd = holeCutTypes.begin()
            + static_cast<std::ptrdiff_t>(std::min(holeCutTypes.size(), cutTypes.size()));
        EXPECT_EQ(Items(holeCutTypes.begin(), fixedEnd), cutTypes);
        EXPECT_EQ(Names(fixedEnd, holeCutTypes.end()), testCase.screwTypes);
        EXPECT_EQ(holeCutTypes.size(), cutTypes.size() + testCase.screwTypes.size());
    }

    auto* hole = newHole();
    const std::vector<std::pair<const App::PropertyEnumeration*, Items>> fixedLists {
        {&hole->ThreadDirection, {"Right", "Left"}},
        {&hole->DepthType, {"Dimension", "ThroughAll"}},
        {&hole->ThreadDepthType, {"Hole Depth", "Dimension", "Tapped (DIN76)"}},
        {&hole->DrillPoint, {"Flat", "Angled"}},
    };
    for (const auto& [property, expected] : fixedLists) {
        SCOPED_TRACE(property->getName());
        EXPECT_EQ(property->getEnumVector(), expected);
    }

    struct TypeSwitchCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        std::string threadSize;
    };
    const std::vector<TypeSwitchCase> switchCases {
        // findClosestDesignation() reads the pitch of the size that setEnums() reset to the first
        // item (0.2 mm), so M8x0.75 wins over M8x1.0.
        {
            "ISO M8x1.25 to ISO fine",
            [](Hole& hole) {
                setThread(hole, "ISOMetricProfile", "M8x1.25");
                setEnum(hole.ThreadType, "ISOMetricFineProfile");
            },
            "M8x0.75",
        },
        {
            "None with diameter 8 to ISO",
            [](Hole& hole) {
                hole.Diameter.setValue(8.0);
                setEnum(hole.ThreadType, "ISOMetricProfile");
            },
            "M8x1.25",
        },
    };
    for (const auto& testCase : switchCases) {
        SCOPED_TRACE(testCase.name);
        auto* switched = newHole();
        testCase.configure(*switched);
        EXPECT_EQ(std::string(switched->ThreadSize.getValueAsString()), testCase.threadSize);
    }

    // Modeled thread with the ISO 4762 screw type, which Restore applies again from the JSON.
    auto* saved = newHole();
    setThread(*saved, "ISOMetricProfile", "M6x1.0");
    setEnum(saved->ThreadClass, "6H");
    saved->Threaded.setValue(true);
    saved->ModelThread.setValue(true);
    saved->Depth.setValue(12.0);
    setEnum(saved->DrillPoint, "Flat");
    setEnum(saved->HoleCutType, "ISO 4762");
    ASSERT_TRUE(recomputeHole(*saved));
    const PropertySnapshot properties = snapshotOf(*saved);
    const ThreadShapeSignature signature = getThreadShapeSignature(saved->AddSubShape.getValue());

    auto* reopened = reopenDocument();
    ASSERT_NE(reopened, nullptr);
    expectSameProperties(snapshotOf(*reopened), properties);
    reopened->touch();
    ASSERT_TRUE(recomputeHole(*reopened));
    expectSameProperties(snapshotOf(*reopened), properties);
    expectSameSignature(
        getThreadShapeSignature(reopened->AddSubShape.getValue()),
        signature,
        analyticTolerance
    );
}

TEST_F(HoleTest, ModeledThreadSignatures)
{
    const double throughAll = getHole()->getThroughAllLength();

    const auto modeledThread = [](Hole& hole,
                                  const std::string& type,
                                  const std::string& size,
                                  const std::string& threadClass) {
        setThread(hole, type, size);
        // Before ModelThread, see DerivedThreadParameters.
        setEnum(hole.ThreadClass, threadClass);
        hole.Threaded.setValue(true);
        hole.ModelThread.setValue(true);
        hole.Depth.setValue(12.0);
        setEnum(hole.DrillPoint, "Flat");
    };
    const auto isoM6 = [modeledThread](Hole& hole, const std::string& threadClass) {
        modeledThread(hole, "ISOMetricProfile", "M6x1.0", threadClass);
    };

    // The rows combine independent code paths: the 5 helix lengths, the 3 profiles (ISO/UTS,
    // Whitworth, ISOTyre), the taper, the 3 clearance modes and the 2 directions.
    struct ThreadCase
    {
        std::string name;
        std::function<void(Hole&)> configure;
        // Hole and thread. Right and left threads differ only in the center of mass, which the
        // 30 mm box would dilute below the tolerance in the final Shape.
        ThreadShapeSignature addSubShape;
        double volume;
        double threadDepth;
    };
    const std::vector<ThreadCase> cases {
        {
            "ISO M6 6H right-handed to the hole depth",
            [&](Hole& hole) { isoM6(hole, "6H"); },
            {317.249840645137,
             777.048827270076,
             gp_Pnt(-0.00606416563317342, -0.0025117844548484, 5.88742799524295),
             -3.0000258648527,
             -3.36105846861186,
             -0.9990001,
             3.26919067533773,
             3.36103165007144,
             12.1239719261433,
             57},
            26723.702702982977,
            12.0,
        },
        {
            "ISO M6 6G left-handed",
            [&](Hole& hole) {
                isoM6(hole, "6G");
                setEnum(hole.ThreadDirection, "Left");
            },
            {320.127089762386,
             781.277471178673,
             gp_Pnt(-0.00607135702354282, 0.00251476349324814, 5.88786525005463),
             -3.01302586483902,
             -3.37559237306029,
             -0.9990001,
             3.28335704866022,
             3.37562613891291,
             12.1239724105887,
             57},
            26721.049740652921,
            12.0,
        },
        {
            "ISO M6 custom clearance tapped (DIN 76)",
            [&](Hole& hole) {
                isoM6(hole, "6H");
                hole.UseCustomThreadClearance.setValue(true);
                hole.CustomThreadClearance.setValue(0.1);
                setEnum(hole.ThreadDepthType, "Tapped (DIN76)");
            },
            {295.945077176376,
             575.349050355257,
             gp_Pnt(-0.00561858325802401, -0.0172918718655431, 5.51930733579339),
             -3.05001512948764,
             -3.41707594827744,
             -0.9990001,
             3.32366502808153,
             3.41702241502297,
             12.0000001,
             37},
            26730.274007595206,
            12.0 - 5.1,
        },
        {
            "ISO M6 thread shorter than the hole",
            [&](Hole& hole) {
                isoM6(hole, "6H");
                setEnum(hole.ThreadDepthType, "Dimension");
                hole.ThreadDepth.setValue(6.0);
            },
            {279.379975963294,
             522.619887184823,
             gp_Pnt(-1.00647332077592e-07, -0.0194762567456646, 5.49093752361921),
             -3.00001298243446,
             -3.36104403269663,
             -0.9990001,
             3.2691766370952,
             3.36101722104593,
             12.0000001,
             33},
            26743.390059797799,
            6.0,
        },
        {
            "ISO M6 through all",
            [&](Hole& hole) {
                isoM6(hole, "6H");
                setEnum(hole.DepthType, "ThroughAll");
            },
            {2781.06857134883,
             6527.12237966021,
             gp_Pnt(0.000229542619283918, -2.73488092639911e-05, 52.6107725467472),
             -3.00023069159262,
             -3.36128463099272,
             -0.9990001,
             3.26941060790795,
             3.36315874263236,
             106.96096982763,
             433},
            26305.990044864036,
            throughAll,
        },
        {
            "BSW 1/4 thread as deep as the hole",
            [&](Hole& hole) {
                modeledThread(hole, "BSW", "1/4", "Medium");
                setEnum(hole.ThreadDepthType, "Dimension");
                hole.ThreadDepth.setValue(12.0);
            },
            {359.441201273893,
             799.463325812391,
             gp_Pnt(0.00608107075743387, -0.0257509469680268, 5.82330956947086),
             -3.19905014310261,
             -3.30867931467436,
             -1.26900054051233,
             3.31699408862165,
             3.30867676442651,
             12.1577187772261,
             45},
            26701.943968986187,
            12.0,
        },
        {
            "ISOTyre 5v1",
            [&](Hole& hole) { modeledThread(hole, "ISOTyre", "5v1", "None"); },
            {255.255929746181,
             718.093219069773,
             gp_Pnt(-0.0036099797573538, -0.0017864341166022, 5.93558873533248),
             -2.69396820907655,
             -3.0181548853852,
             -0.7040001,
             2.93567373486994,
             3.01812405396307,
             12.0871055123348,
             77},
            26771.323727061226,
            12.0,
        },
        {
            "NPT 1/8 tapered",
            [&](Hole& hole) {
                modeledThread(hole, "NPT", "1/8", "None");
                hole.Tapered.setValue(true);
            },
            {864.471835412556,
             1434.38740752448,
             gp_Pnt(0.00564455406895819, -0.00378576853301537, 5.77766708074334),
             -5.1435001,
             -5.75518345595965,
             -0.9400001,
             5.58896107623016,
             5.73869395280375,
             12.1166097472354,
             57},
            26186.792322967718,
            12.0,
        },
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        auto* hole = newHole();
        testCase.configure(*hole);
        if (!recomputeHole(*hole)) {
            continue;
        }
        expectSameSignature(getThreadShapeSignature(hole->AddSubShape.getValue()), testCase.addSubShape);
        EXPECT_NEAR(volumeOf(hole->Shape.getValue()), testCase.volume, shapeTolerance);
        // execute() writes ThreadDepth unless ThreadDepthType is Dimension.
        EXPECT_NEAR(hole->ThreadDepth.getValue(), testCase.threadDepth, valueTolerance);
    }
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
