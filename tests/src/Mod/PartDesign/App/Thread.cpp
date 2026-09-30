// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeatureThread.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

static int findEnumIndex(const std::vector<std::string>& enums, const std::string& needle)
{
    for (size_t i = 0; i < enums.size(); ++i) {
        if (enums[i].find(needle) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

class ThreadTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Thread_test", "testUser");
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addObject(_sketch);

        _sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        _sketch->MapMode.setValue("FlatFace");

        Part::GeomCircle circle;
        circle.setRadius(10.0);
        _sketch->addGeometry(&circle, false);
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
        }
    }

    App::Document* getDocument() const
    {
        return _doc;
    }

    PartDesign::Body* getBody() const
    {
        return _body;
    }

    Sketcher::SketchObject* getSketch() const
    {
        return _sketch;
    }

    static void addProfileLoop(
        Sketcher::SketchObject* sketch,
        const std::vector<Base::Vector3d>& points,
        bool close = true
    )
    {
        const size_t edgeCount = close ? points.size() : points.size() - 1;
        for (size_t i = 0; i < edgeCount; ++i) {
            auto line = std::make_unique<Part::GeomLineSegment>();
            line->setPoints(points[i], points[(i + 1) % points.size()]);
            sketch->addGeometry(std::move(line));
        }
    }

    Sketcher::SketchObject* createThreadProfile(
        const char* name,
        const std::vector<Base::Vector3d>& points,
        bool close = true
    )
    {
        auto* profile = _doc->addObject<Sketcher::SketchObject>(name);
        addProfileLoop(profile, points, close);
        _doc->recompute();
        return profile;
    }

    static TopoDS_Wire getProfileWire(const Sketcher::SketchObject* profile)
    {
        return TopoDS::Wire(profile->Shape.getShape().getShape());
    }

    PartDesign::Pad* createCylinderPad(double length = 30.0, double radius = 10.0)
    {
        auto doc = getDocument();
        auto body = getBody();
        auto sketch = getSketch();

        // Overwrite whatever geometry SetUp() left in the sketch (a radius-10.0
        // circle) so callers can request an arbitrary cylinder radius. This
        // mirrors how createCubePad() replaces the sketch contents below.
        sketch->Geometry.setValues({});
        sketch->Constraints.setValues({});

        Part::GeomCircle circle;
        circle.setRadius(radius);
        sketch->addGeometry(&circle, false);

        auto pad = doc->addObject<PartDesign::Pad>("Pad");
        body->addObject(pad);
        pad->Profile.setValue(sketch, {""});
        pad->Direction.setValue(0.0, 0.0, 1.0);
        pad->Length.setValue(length);
        pad->Midplane.setValue(false);

        doc->recompute();
        return pad;
    }

    PartDesign::Pad* createCubePad(double sideLength = 30.0)
    {
        auto doc = getDocument();
        auto body = getBody();
        auto sketch = getSketch();

        sketch->Geometry.setValues({});
        sketch->Constraints.setValues({});

        double half = sideLength / 2.0;

        auto l1 = std::make_unique<Part::GeomLineSegment>();
        l1->setPoints(Base::Vector3d(-half, -half, 0.0), Base::Vector3d(half, -half, 0.0));
        sketch->addGeometry(std::move(l1));

        auto l2 = std::make_unique<Part::GeomLineSegment>();
        l2->setPoints(Base::Vector3d(half, -half, 0.0), Base::Vector3d(half, half, 0.0));
        sketch->addGeometry(std::move(l2));

        auto l3 = std::make_unique<Part::GeomLineSegment>();
        l3->setPoints(Base::Vector3d(half, half, 0.0), Base::Vector3d(-half, half, 0.0));
        sketch->addGeometry(std::move(l3));

        auto l4 = std::make_unique<Part::GeomLineSegment>();
        l4->setPoints(Base::Vector3d(-half, half, 0.0), Base::Vector3d(-half, -half, 0.0));
        sketch->addGeometry(std::move(l4));

        int pairs[4][4] = {{0, 2, 1, 1}, {1, 2, 2, 1}, {2, 2, 3, 1}, {3, 2, 0, 1}};

        for (int i = 0; i < 4; ++i) {
            auto c = new Sketcher::Constraint();
            c->Type = Sketcher::Coincident;
            c->First = pairs[i][0];
            c->FirstPos = static_cast<Sketcher::PointPos>(pairs[i][1]);
            c->Second = pairs[i][2];
            c->SecondPos = static_cast<Sketcher::PointPos>(pairs[i][3]);
            sketch->addConstraint(c);
        }

        auto pad = doc->addObject<PartDesign::Pad>("Pad");
        body->addObject(pad);
        pad->Profile.setValue(sketch, {""});
        pad->Direction.setValue(0.0, 0.0, 1.0);
        pad->Length.setValue(sideLength);
        pad->Midplane.setValue(false);

        doc->recompute();
        return pad;
    }

    std::optional<std::string> getLateralFaceName(PartDesign::Pad* pad)
    {
        const TopoDS_Shape& shape = Part::Feature::getShape(pad, Part::ShapeOption::NoFlag);

        const Part::TopoShape& topo = pad->Shape.getShape();

        for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
            TopoDS_Face face = TopoDS::Face(exp.Current());

            BRepAdaptor_Surface surface(face);

            if (surface.GetType() == GeomAbs_Cylinder) {
                int idx = topo.findShape(face);
                if (idx > 0) {
                return "Face" + std::to_string(idx);
                }
            }
        }

        return std::nullopt;
    }

private:
    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
};

TEST_F(ThreadTest, ThreadProfileLoaderCopiesValidNormalizedProfile)
{
    createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        }
    );

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    ASSERT_EQ(definition.profileStatus, Status::Valid) << definition.profileDiagnostic;
    ASSERT_TRUE(definition.profile.has_value());
    EXPECT_EQ(definition.externalProfileStatus, Status::Missing);
    EXPECT_TRUE(definition.externalProfileDiagnostic.find("was not found") != std::string::npos);
    EXPECT_NEAR(definition.profile->minX, -0.75, 1e-9);
    EXPECT_NEAR(definition.profile->maxX, 0.0, 1e-9);
    EXPECT_NEAR(definition.profile->minY, 0.001, 1e-9);
    EXPECT_NEAR(definition.profile->maxY, 0.999, 1e-9);
    EXPECT_TRUE(definition.profile->wire.isClosed());

    getDocument()->removeObject("ThreadProfile");
    getDocument()->recompute();
    EXPECT_FALSE(definition.profile->wire.isNull());
    EXPECT_TRUE(definition.profile->wire.isValid());
}

TEST_F(ThreadTest, ThreadProfileLoaderReadsOptionalExternalProfile)
{
    const std::vector<Base::Vector3d> commonProfile {
        Base::Vector3d(-0.75, 0.001, 0.0),
        Base::Vector3d(0.0, 0.4375, 0.0),
        Base::Vector3d(0.0, 0.5625, 0.0),
        Base::Vector3d(-0.75, 0.999, 0.0),
    };
    createThreadProfile("ThreadProfile", commonProfile);

    auto externalProfile = commonProfile;
    externalProfile[0].x = -0.8;
    externalProfile[3].x = -0.8;
    createThreadProfile("ExternalThreadProfile", externalProfile);

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    ASSERT_EQ(definition.profileStatus, Status::Valid) << definition.profileDiagnostic;
    ASSERT_EQ(definition.externalProfileStatus, Status::Valid)
        << definition.externalProfileDiagnostic;
    ASSERT_TRUE(definition.externalProfile.has_value());
    EXPECT_NEAR(definition.externalProfile->minX, -0.8, 1e-9);
    EXPECT_EQ(definition.sketches.size(), 2);
}

TEST_F(ThreadTest, ThreadProfileLoaderRejectsOpenProfile)
{
    createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        },
        false
    );

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_FALSE(definition.profile.has_value());
    EXPECT_TRUE(definition.profileDiagnostic.find("closed wire") != std::string::npos);
}

TEST_F(ThreadTest, ThreadProfileLoaderRejectsMultipleWires)
{
    auto* profile = createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        }
    );
    addProfileLoop(
        profile,
        {
            Base::Vector3d(-0.5, 0.2, 0.0),
            Base::Vector3d(-0.4, 0.2, 0.0),
            Base::Vector3d(-0.4, 0.3, 0.0),
            Base::Vector3d(-0.5, 0.3, 0.0),
        }
    );
    getDocument()->recompute();

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_TRUE(definition.profileDiagnostic.find("exactly one connected wire")
                != std::string::npos);
}

TEST_F(ThreadTest, ThreadProfileLoaderRejectsClockwiseProfile)
{
    createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, 0.999, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(-0.75, 0.001, 0.0),
        }
    );

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_TRUE(definition.profileDiagnostic.find("counter-clockwise") != std::string::npos);
}

TEST_F(ThreadTest, ThreadProfileLoaderRejectsNonNormalizedAndPlacedProfiles)
{
    auto* profile = createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, -0.1, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        }
    );

    PartDesign::ThreadUtils::ThreadDefinition definition;
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");

    using Status = PartDesign::ThreadUtils::ThreadDefinition::ProfileStatus;
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_TRUE(definition.profileDiagnostic.find("range 0..1") != std::string::npos);

    profile->Placement.setValue(Base::Placement(Base::Vector3d(0.0, 0.0, 1.0), Base::Rotation()));
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_TRUE(definition.profileDiagnostic.find("XY plane") != std::string::npos);

    getDocument()->removeObject("ThreadProfile");
    createThreadProfile(
        "ThreadProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(-0.1, 0.4375, 0.0),
            Base::Vector3d(-0.1, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        }
    );
    PartDesign::ThreadUtils::findThreadProfiles(getDocument(), definition, "profile-test");
    EXPECT_EQ(definition.profileStatus, Status::Invalid);
    EXPECT_TRUE(definition.profileDiagnostic.find("X = 0") != std::string::npos);
}

TEST_F(ThreadTest, ThreadEndTrimKeepsPreferredEighthPitchWhenSafe)
{
    auto* profile = createThreadProfile(
        "TrimProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        }
    );

    constexpr double pitch = 2.0;
    EXPECT_NEAR(
        PartDesign::ThreadUtils::calculateThreadEndTrim(getProfileWire(profile), pitch),
        pitch / 8.0,
        1e-9
    );
}

TEST_F(ThreadTest, ThreadEndTrimAvoidsWhitworthRootTangentVertices)
{
    auto* profile = createThreadProfile(
        "TrimProfile",
        {
            Base::Vector3d(-0.8, 0.0, 0.0),
            Base::Vector3d(-0.64, 0.125, 0.0),
            Base::Vector3d(-0.08, 0.375, 0.0),
            Base::Vector3d(0.0, 0.5, 0.0),
            Base::Vector3d(-0.08, 0.625, 0.0),
            Base::Vector3d(-0.64, 0.875, 0.0),
            Base::Vector3d(-0.8, 1.0, 0.0),
        }
    );

    constexpr double pitch = 2.0;
    EXPECT_NEAR(
        PartDesign::ThreadUtils::calculateThreadEndTrim(getProfileWire(profile), pitch),
        pitch / 4.0,
        1e-9
    );
}

TEST_F(ThreadTest, ThreadEndTrimChecksBothEndsForAsymmetricProfiles)
{
    auto* profile = createThreadProfile(
        "TrimProfile",
        {
            Base::Vector3d(-0.75, 0.0, 0.0),
            Base::Vector3d(0.0, 0.3, 0.0),
            Base::Vector3d(0.0, 0.6, 0.0),
            Base::Vector3d(-0.2, 0.875, 0.0),
            Base::Vector3d(-0.75, 1.0, 0.0),
        }
    );

    constexpr double pitch = 2.0;
    EXPECT_NEAR(
        PartDesign::ThreadUtils::calculateThreadEndTrim(getProfileWire(profile), pitch),
        0.2125 * pitch,
        1e-9
    );
}

TEST_F(ThreadTest, ThreadEndTrimRejectsInvalidInputs)
{
    auto* openProfile = createThreadProfile(
        "OpenTrimProfile",
        {
            Base::Vector3d(-0.75, 0.001, 0.0),
            Base::Vector3d(0.0, 0.4375, 0.0),
            Base::Vector3d(0.0, 0.5625, 0.0),
            Base::Vector3d(-0.75, 0.999, 0.0),
        },
        false
    );

    EXPECT_THROW(
        PartDesign::ThreadUtils::calculateThreadEndTrim(TopoDS_Wire(), 1.0),
        Base::ValueError
    );
    EXPECT_THROW(
        PartDesign::ThreadUtils::calculateThreadEndTrim(getProfileWire(openProfile), 1.0),
        Base::ValueError
    );
    EXPECT_THROW(
        PartDesign::ThreadUtils::calculateThreadEndTrim(getProfileWire(openProfile), 0.0),
        Base::ValueError
    );
}

TEST_F(ThreadTest, ThreadCreationOnCylinder)
{
    auto doc = getDocument();
    auto body = getBody();
    //TODO: now that threadtype matters, change magic numbers for automatic diameter
    auto pad = createCylinderPad(30.0, 12.7);
    ASSERT_NE(pad, nullptr);
    auto thread = doc->addObject<PartDesign::Thread>("Thread");
    body->addObject(thread);

    auto lateralFace = getLateralFaceName(pad);
    thread->LateralFace.setValue(pad, {*lateralFace});

    doc->recompute();

    ASSERT_NE(thread, nullptr);
    EXPECT_FALSE(thread->isError())
        << "Feature thread has failed during recompute: " << thread->getStatusString();
    EXPECT_TRUE(thread->isValid()) << "Feature Thread is not valid.";
}

TEST_F(ThreadTest, ThreadCreationOnCube)
{
    auto doc = getDocument();
    auto body = getBody();
    auto pad = createCubePad(30.0);
    ASSERT_NE(pad, nullptr);
    auto thread = doc->addObject<PartDesign::Thread>("Thread");
    body->addObject(thread);
    thread->LateralFace.setValue(pad, {"Face3"});  // any cube face works

    doc->recompute();

    ASSERT_NE(thread, nullptr);
    EXPECT_TRUE(thread->isError())
        << "Feature Thread should have failed for a plane face, but didn't failed.";
    EXPECT_FALSE(thread->isValid());
}

TEST_F(ThreadTest, EmptyThread)
{
    auto doc = getDocument();
    auto body = getBody();
    auto pad = createCylinderPad(30.0);
    ASSERT_NE(pad, nullptr);
    auto thread = doc->addObject<PartDesign::Thread>("Thread");
    body->addObject(thread);

    doc->recompute();

    ASSERT_NE(thread, nullptr);
    EXPECT_FALSE(thread->isError())
        << "Feature thread has failed during recompute " << thread->getStatusString();
    EXPECT_TRUE(thread->isValid()) << "Feature Thread is not valid.";
}

// TEST_F(ThreadTest, ExternalCosmeticThreadPreservesBase)
// {
//     auto doc = getDocument();
//     auto body = getBody();
//     auto pad = createCylinderPad(24.0, 12.0);
//     ASSERT_NE(pad, nullptr);

//     auto thread = doc->addObject<PartDesign::Thread>("Thread");
//     body->addObject(thread);

//     auto lateralFace = getLateralFaceName(pad);
//     ASSERT_TRUE(lateralFace.has_value());
//     thread->LateralFace.setValue(pad, {*lateralFace});
//     thread->DepthType.setValue(0L);  // "Dimension"
//     thread->Depth.setValue(24.0);

//     int typeIdx = findEnumIndex(thread->ThreadType.getEnumVector(), "ISOMetricProfile");
//     ASSERT_GE(typeIdx, 0);
//     thread->ThreadType.setValue(typeIdx);

//     int sizeIdx = findEnumIndex(thread->ThreadSize.getEnumVector(), "24");
//     ASSERT_GE(sizeIdx, 0);
//     thread->ThreadSize.setValue(sizeIdx);

//     int pitchIdx = findEnumIndex(thread->ThreadSizePitch.getEnumVector(), "3");
//     ASSERT_GE(pitchIdx, 0);
//     thread->ThreadSizePitch.setValue(pitchIdx);

//     thread->ModelThread.setValue(false);
//     thread->CosmeticThread.setValue(true);
//     doc->recompute();

//     ASSERT_FALSE(thread->isError()) << thread->getStatusString();
//     ASSERT_TRUE(thread->isValid());
//     EXPECT_FALSE(thread->IsInternal.getValue());
//     EXPECT_TRUE(thread->Shape.getValue().isSame(pad->Shape.getValue()));
//     EXPECT_TRUE(thread->AddSubShape.getValue().isNull());
//     EXPECT_TRUE(thread->getReducedBasePreviewShape().isNull());

//     GProp_GProps padVolume;
//     GProp_GProps threadVolume;
//     BRepGProp::VolumeProperties(pad->Shape.getValue(), padVolume);
//     BRepGProp::VolumeProperties(thread->Shape.getValue(), threadVolume);
//     EXPECT_NEAR(threadVolume.Mass(), padVolume.Mass(), 1e-9);

//     GProp_GProps padSurface;
//     GProp_GProps threadSurface;
//     BRepGProp::SurfaceProperties(pad->Shape.getValue(), padSurface);
//     BRepGProp::SurfaceProperties(thread->Shape.getValue(), threadSurface);
//     EXPECT_NEAR(threadSurface.Mass(), padSurface.Mass(), 1e-9);
// }

TEST_F(ThreadTest, ExternalThreadModeledM24Signature)
{
    auto doc = getDocument();
    auto body = getBody();
    auto pad = createCylinderPad(24.0, 12.0);  // 24mm Height, 24mm diameter
    ASSERT_NE(pad, nullptr);

    auto thread = doc->addObject<PartDesign::Thread>("Thread");
    body->addObject(thread);

    auto lateralFace = getLateralFaceName(pad);
    ASSERT_TRUE(lateralFace.has_value());
    thread->LateralFace.setValue(pad, {*lateralFace});

    thread->DepthType.setValue(0L); // "Dimension"
    thread->Depth.setValue(24.0);

    int typeIdx = findEnumIndex(thread->ThreadType.getEnumVector(), "ISOMetricProfile");
    ASSERT_GE(typeIdx, 0) << "ISOMetricProfile was not found in enum";
    thread->ThreadType.setValue(typeIdx); // ISOMetricProfile

    int sizeIdx = findEnumIndex(thread->ThreadSize.getEnumVector(), "24");
    ASSERT_GE(sizeIdx, 0) << "24mm Diameter was not found in enum";
    thread->ThreadSize.setValue(sizeIdx);

    int pitchIdx = findEnumIndex(thread->ThreadSizePitch.getEnumVector(), "3");
    ASSERT_GE(pitchIdx, 0) << "3mm Pitch was not found in enum";
    thread->ThreadSizePitch.setValue(pitchIdx);

    thread->UseCustomThreadClearance.setValue(true);
    thread->CustomThreadClearance.setValue(0.0);

    thread->ModelThread.setValue(true);
    thread->CosmeticThread.setValue(false);

    doc->recompute();

    ASSERT_FALSE(thread->isError()) << thread->getStatusString();
    ASSERT_TRUE(thread->isValid());

    const TopoDS_Shape& shape = thread->Shape.getValue();

    GProp_GProps volProps;
    BRepGProp::VolumeProperties(shape, volProps);
    EXPECT_NEAR(volProps.Mass(), 9185.1339280588, 1e-3);

    GProp_GProps surfProps;
    BRepGProp::SurfaceProperties(shape, surfProps);
    EXPECT_NEAR(surfProps.Mass(), 3299.5175979113, 1e-3);

    gp_Pnt com = volProps.CentreOfMass();
    EXPECT_NEAR(com.X(), -0.0280991554, 1e-3);
    EXPECT_NEAR(com.Y(), -0.0280997884, 1e-3);
    EXPECT_NEAR(com.Z(), 12.0000050577, 1e-3);

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    
    EXPECT_NEAR(xmin, -12.0000454873, 1e-3);
    EXPECT_NEAR(xmax, 13.0767229177, 1e-3);
    EXPECT_NEAR(ymin, -13.4439280178, 1e-3);
    EXPECT_NEAR(ymax, 13.4439666164, 1e-3);
    EXPECT_NEAR(zmin, -0.3740050000, 1e-3);
    EXPECT_NEAR(zmax, 24.3739560982, 1e-3);

    int nFaces = 0, nEdges = 0, nVertices = 0;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) ++nFaces;
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) ++nEdges;
    for (TopExp_Explorer e(shape, TopAbs_VERTEX); e.More(); e.Next()) ++nVertices;
    EXPECT_EQ(nFaces, 37);
    EXPECT_EQ(nEdges, 152);
    EXPECT_EQ(nVertices, 304);

    EXPECT_NEAR(thread->Diameter.getValue(), 24.0, 1e-9);
    EXPECT_FALSE(thread->IsInternal.getValue());
    EXPECT_EQ(std::string(thread->ThreadDesignation.getValue()), "M24x3.0");
}

TEST_F(ThreadTest, ExternalThreadModeledM24FineSignature)
{
    auto doc = getDocument();
    auto body = getBody();
    auto pad = createCylinderPad(24.0, 12.0);  // 24mm Height, 24mm diameter
    ASSERT_NE(pad, nullptr);

    auto thread = doc->addObject<PartDesign::Thread>("Thread");
    body->addObject(thread);

    auto lateralFace = getLateralFaceName(pad);
    ASSERT_TRUE(lateralFace.has_value());
    thread->LateralFace.setValue(pad, {*lateralFace});

    thread->DepthType.setValue(0L); // "Dimension"
    thread->Depth.setValue(24.0);

    int typeIdx = findEnumIndex(thread->ThreadType.getEnumVector(), "ISOMetricFineProfile");
    ASSERT_GE(typeIdx, 0) << "ISOMetricFineProfile was not found in enum";
    thread->ThreadType.setValue(typeIdx); // ISOMetricFineProfile

    int sizeIdx = findEnumIndex(thread->ThreadSize.getEnumVector(), "24");
    ASSERT_GE(sizeIdx, 0) << "24mm Diameter was not found in enum";
    thread->ThreadSize.setValue(sizeIdx);

    int pitchIdx = findEnumIndex(thread->ThreadSizePitch.getEnumVector(), "2");
    ASSERT_GE(pitchIdx, 0) << "2mm Pitch was not found in enum";
    thread->ThreadSizePitch.setValue(pitchIdx);

    thread->UseCustomThreadClearance.setValue(true);
    thread->CustomThreadClearance.setValue(0.0);

    thread->ModelThread.setValue(true);
    thread->CosmeticThread.setValue(false);

    doc->recompute();

    ASSERT_FALSE(thread->isError()) << thread->getStatusString();
    ASSERT_TRUE(thread->isValid());

    const TopoDS_Shape& shape = thread->Shape.getValue();

    GProp_GProps volProps;
    BRepGProp::VolumeProperties(shape, volProps);
    EXPECT_NEAR(volProps.Mass(), 9747.8530376664, 1e-3);

    GProp_GProps surfProps;
    BRepGProp::SurfaceProperties(shape, surfProps);
    EXPECT_NEAR(surfProps.Mass(), 3473.5609671334, 1e-3);

    gp_Pnt com = volProps.CentreOfMass();
    EXPECT_NEAR(com.X(), -0.0123890002, 1e-3);
    EXPECT_NEAR(com.Y(), -0.0123917224, 1e-3);
    EXPECT_NEAR(com.Z(), 12.0000090125, 1e-3);

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    EXPECT_NEAR(xmin, -12.0000267147, 1e-3);
    EXPECT_NEAR(xmax, 13.0767083783, 1e-3);
    EXPECT_NEAR(ymin, -13.4439239228, 1e-3);
    EXPECT_NEAR(ymax, 13.4439410928, 1e-3);
    EXPECT_NEAR(zmin, -0.2490001000, 1e-3);
    EXPECT_NEAR(zmax, 24.2489667767, 1e-3);

    int nFaces = 0, nEdges = 0, nVertices = 0;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) ++nFaces;
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) ++nEdges;
    for (TopExp_Explorer e(shape, TopAbs_VERTEX); e.More(); e.Next()) ++nVertices;
    EXPECT_EQ(nFaces, 52);
    EXPECT_EQ(nEdges, 208);
    EXPECT_EQ(nVertices, 416);

    EXPECT_NEAR(thread->Diameter.getValue(), 24.0, 1e-9);
    EXPECT_FALSE(thread->IsInternal.getValue());
    EXPECT_EQ(std::string(thread->ThreadDesignation.getValue()), "M24x2.0");
}

// TEST_F(ThreadTest, ExternalThreadModeledBSPHalfInchLegacySignature)
// {
//     constexpr double majorDiameter = 20.955;
//     constexpr double length = 24.0;

//     auto doc = getDocument();
//     auto body = getBody();
//     auto pad = createCylinderPad(length, majorDiameter / 2.0);
//     ASSERT_NE(pad, nullptr);

//     auto thread = doc->addObject<PartDesign::Thread>("Thread");
//     body->addObject(thread);

//     auto lateralFace = getLateralFaceName(pad);
//     ASSERT_TRUE(lateralFace.has_value());
//     thread->LateralFace.setValue(pad, {*lateralFace});

//     thread->DepthType.setValue(0L);  // "Dimension"
//     thread->Depth.setValue(length);

//     int typeIdx = findEnumIndex(thread->ThreadType.getEnumVector(), "BSP");
//     ASSERT_GE(typeIdx, 0) << "BSP was not found in enum";
//     thread->ThreadType.setValue(typeIdx);

//     int sizeIdx = findEnumIndex(thread->ThreadSize.getEnumVector(), "20.955");
//     ASSERT_GE(sizeIdx, 0) << "BSP 1/2 major diameter was not found in enum";
//     thread->ThreadSize.setValue(sizeIdx);

//     int pitchIdx = findEnumIndex(thread->ThreadSizePitch.getEnumVector(), "1.814");
//     ASSERT_GE(pitchIdx, 0) << "BSP 1/2 pitch was not found in enum";
//     thread->ThreadSizePitch.setValue(pitchIdx);

//     thread->UseCustomThreadClearance.setValue(true);
//     thread->CustomThreadClearance.setValue(0.0);
//     thread->ModelThread.setValue(true);
//     thread->CosmeticThread.setValue(false);

//     doc->recompute();

//     ASSERT_FALSE(thread->isError()) << thread->getStatusString();
//     ASSERT_TRUE(thread->isValid());

//     const TopoDS_Shape& shape = thread->Shape.getValue();

//     GProp_GProps volProps;
//     BRepGProp::VolumeProperties(shape, volProps);
//     GProp_GProps surfProps;
//     BRepGProp::SurfaceProperties(shape, surfProps);
//     const gp_Pnt com = volProps.CentreOfMass();

//     Bnd_Box box;
//     BRepBndLib::Add(shape, box);
//     double xmin, ymin, zmin, xmax, ymax, zmax;
//     box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

//     int nFaces = 0;
//     int nEdges = 0;
//     int nVertices = 0;
//     for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) ++nFaces;
//     for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) ++nEdges;
//     for (TopExp_Explorer e(shape, TopAbs_VERTEX); e.More(); e.Next()) ++nVertices;

//     EXPECT_NEAR(volProps.Mass(), 7290.0134362847, 1e-3);
//     EXPECT_NEAR(surfProps.Mass(), 3291.61701096588, 1e-3);

//     EXPECT_NEAR(com.X(), -0.00234325479802929, 1e-3);
//     EXPECT_NEAR(com.Y(), -0.0380918823304115, 1e-3);
//     EXPECT_NEAR(com.Z(), 12.000003064811, 1e-3);

//     EXPECT_NEAR(xmin, -10.5118429167223, 1e-3);
//     EXPECT_NEAR(ymin, -11.7766716237672, 1e-3);
//     EXPECT_NEAR(zmin, -0.225755, 1e-3);
//     EXPECT_NEAR(xmax, 10.9834194583173, 1e-3);
//     EXPECT_NEAR(ymax, 10.7476288112927, 1e-3);
//     EXPECT_NEAR(zmax, 24.2257219690522, 1e-3);

//     EXPECT_EQ(nFaces, 57);
//     EXPECT_EQ(nEdges, 232);
//     EXPECT_EQ(nVertices, 464);

//     EXPECT_NEAR(thread->Diameter.getValue(), majorDiameter, 1e-9);
//     EXPECT_FALSE(thread->IsInternal.getValue());
//     EXPECT_FALSE(shape.IsNull());
// }

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
