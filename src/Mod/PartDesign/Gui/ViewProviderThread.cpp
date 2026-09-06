// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Caio Venâncio <caio.venancio784@gmail.com>          *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include <Mod/PartDesign/App/FeatureThread.h>
#include <App/PropertyStandard.h>

#include <QMenu>
#include <QMessageBox>

#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Poly_Triangle.hxx>

#include <BRep_Tool.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Material.h>
#include <Gui/Application.h>
#include <Gui/ViewProvider.h>
#include <Mod/Part/App/Tools.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/Feature.h>

#include <Base/Placement.h>
#include <Base/Tools.h>
#include <App/Property.h>
#include <Utilities.h>

#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoNormal.h>
#include <Inventor/nodes/SoNormalBinding.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTransparencyType.h>

#include "TaskThreadParameters.h"
#include "ViewProviderThread.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderThread, PartDesignGui::ViewProviderDressUp)

bool DEBUG = false;

ViewProviderThread::ViewProviderThread()
    : textureExtension(std::make_unique<Gui::ViewProviderTextureExtension>())
{
    sPixmap = "PartDesign_Thread.svg";
    menuName = tr("Thread Parameters");
}


// ViewProviderThread::~ViewProviderThread() = default;

// bool ViewProviderHole::onDelete(const std::vector<std::string>& arg)
// {
    // clearThreadTextures();
    // return PartDesignGui::ViewProviderDressUp::onDelete(arg);
// }

const std::string& ViewProviderThread::featureName() const
{
    static const std::string name = "Thread";
    return name;
}

void ViewProviderThread::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit Thread"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

TaskDlgFeatureParameters* ViewProviderThread::getEditDialog()
{
    return new TaskDlgThreadParameters(this);
}

void ViewProviderThread::updateData(const App::Property* prop)
{
    PartDesignGui::ViewProviderDressUp::updateData(prop);
    
    auto* pcThread = getObject<PartDesign::Thread>();
    if(!pcThread || !prop) {
        if (DEBUG) Base::Console().message("exit empty\n");
        return;
    }
    
    if (prop == &pcThread->Threaded || prop == &pcThread->CosmeticThread || prop == &pcThread->ModelThread) {
        if (DEBUG) Base::Console().message("I'm running!!!: %s\n", prop->getName());
        // if (DEBUG) Base::Console().message("O prop sou eu!\n");
        if (pcThread->getParents().empty()) {
            if (DEBUG) Base::Console().message("exit parents empty\n");
            return;
        }
        updateOverlay();
        if (DEBUG) Base::Console().message("exit update overlay\n");
        return;
    }
    if (prop == &pcThread->ThreadDepth || prop == &pcThread->ThreadDepthType) {
        updateThreadClipper(pcThread);
        if (DEBUG) Base::Console().message("exit thread clipper\n");
        return;
    }
    if(prop == &pcThread->ThreadDirection) {
        updateThreadDirection(pcThread);
        if (DEBUG) Base::Console().message("exit thread direction\n");
        return;
    }
    // if (DEBUG) Base::Console().message("I'm ending!!!\n");
}

SoSeparator* ViewProviderThread::createThreadTextureSeparator()
{
    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Starting texture separator creation...\n");

    auto* pcThread = getObject<PartDesign::Thread>();
    if (!pcThread) {
        if (DEBUG) Base::Console().warning("[createThreadTextureSeparator]: Thread object is null -> nullptr\n");
        return nullptr;
    }

    gp_Pnt threadOriginPnt;
    auto threadOriginOpt = getThreadOrigin(pcThread);
    if (!threadOriginOpt.has_value()) {
        if (DEBUG) Base::Console().warning("[createThreadTextureSeparator]: Failed to retrieve thread origin for '%s' -> nullptr\n", pcThread->getNameInDocument());
        return nullptr;
    }
    threadOriginPnt = *threadOriginOpt;

    std::vector<SbVec3f> vertices;
    std::vector<SbVec3f> normals;
    std::vector<int> indices;
    std::vector<SbVec2f> uvs;

    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Generating bore mesh data for '%s'...\n", pcThread->getNameInDocument());

    if (!generateBoreMeshData(pcThread, threadOriginPnt, vertices, normals, indices, uvs)
        || vertices.empty() || normals.empty() || indices.empty() || uvs.empty()) {
        if (DEBUG) Base::Console().warning("[createThreadTextureSeparator]: Mesh generation failed or produced empty data for '%s' (Verts: %zu, Norms: %zu, Inds: %zu, UVs: %zu) -> nullptr\n",
                                pcThread->getNameInDocument(),
                                vertices.size(), normals.size(), indices.size(), uvs.size());
        return nullptr;
    }

    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Mesh data generated successfully (Verts: %zu, Inds: %zu). Assembling Coin3D nodes...\n",
                            vertices.size(), indices.size());

    // Create subtree
    auto* threadSep = new SoSeparator();
    threadSep->ref();
    
    // The face is selectable but not the texture
    auto* pickStyle = new SoPickStyle();
    pickStyle->style = SoPickStyle::UNPICKABLE;
    threadSep->addChild(pickStyle);
    
    // Avoid flicker on transparent objects
    auto* tt = new SoTransparencyType();
    tt->value = SoTransparencyType::DELAYED_BLEND;
    threadSep->addChild(tt);

    // End Clipping plane
    m_endThreadClipper = new SoClipPlane();
    threadSep->addChild(m_endThreadClipper);

    // Material
    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Setting up material...\n");
    auto* mat = new SoMaterial();
    if (textureExtension) {
        App::Material globalMat = getGlobalMaterial();
        if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Applying coin appearance from textureExtension...\n");
        textureExtension->setCoinAppearance(mat, globalMat);
    } else {
        if (DEBUG) Base::Console().warning("[createThreadTextureSeparator]: textureExtension pointer is NULL! Skipping appearance setup.\n");
    }
    threadSep->addChild(mat);

    // Texture
    auto* threadTexture = new SoTexture2();
    threadTexture->filename.setValue(":/images/ThreadOverlay.png");
    threadTexture->wrapS = SoTexture2::REPEAT;
    threadTexture->wrapT = SoTexture2::REPEAT;
    threadSep->addChild(threadTexture);

    // --- Texture transform for flipping ---
    m_textureTransform = new SoTexture2Transform();
    updateThreadDirection(pcThread);  // apply initial direction
    threadSep->addChild(m_textureTransform);

    // Texcoords / normals / geometry
    auto* tc = new SoTextureCoordinate2();
    tc->point.setValues(0, (int)uvs.size(), uvs.data());
    threadSep->addChild(tc);

    auto* nb = new SoNormalBinding();
    nb->value = SoNormalBinding::PER_VERTEX_INDEXED;
    threadSep->addChild(nb);

    auto* ns = new SoNormal();
    ns->vector.setValues(0, (int)normals.size(), normals.data());
    threadSep->addChild(ns);

    auto* coords = new SoCoordinate3();
    coords->point.setValues(0, (int)vertices.size(), vertices.data());
    threadSep->addChild(coords);

    auto* faces = new SoIndexedFaceSet();
    faces->coordIndex.setValues(0, (int)indices.size(), indices.data());
    threadSep->addChild(faces);

    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Updating clipper and phase offset...\n");
    updateThreadClipper(pcThread);
    applyThreadPhaseOffset(pcThread);

    if (DEBUG) Base::Console().message("[createThreadTextureSeparator]: Successfully created thread texture separator for '%s'\n", pcThread->getNameInDocument());

    return threadSep;
}

void ViewProviderThread::updateThreadDirection(const PartDesign::Thread* pcThread)
{
    if (!pcThread || !m_textureTransform) {
        return;
    }
    if (pcThread->ThreadDirection.getValue() == 0) {
        m_textureTransform->scaleFactor.setValue(SbVec2f(-1.0F, 1.0F));
    }
    else {
        m_textureTransform->scaleFactor.setValue(SbVec2f(1.0F, 1.0F));
    }
}

void ViewProviderThread::applyThreadPhaseOffset(const PartDesign::Thread* pcThread)
{
    if (!pcThread || !m_textureTransform) {
        return;
    }
    // Applies a unique offset so overlapping threads can be shown as crossed
    // Uses a stable hash of the hole name so it's deterministic between runs
    const std::string key = pcThread->getNameInDocument();
    unsigned hash = std::hash<std::string> {}(key);
    // Map hash to 0..1 range for UV offset
    constexpr float invMax = 1.0F / static_cast<float>(std::numeric_limits<unsigned>::max());
    const float phase = static_cast<float>(hash) * invMax;
    // Apply only horizontal (U) offset
    m_textureTransform->translation.setValue(SbVec2f(phase, 0.0F));
}

// void ViewProviderThread::updateThreadClipper(const PartDesign::Thread* pcThread)
// {
//     if (!pcThread || pcThread->isRecomputing() || !m_endThreadClipper) {
//         return;
//     }
//     std::string theadDepthType = pcThread->ThreadDepthType.getValueAsString();
//     if (theadDepthType == "Hole depth") {
//         m_endThreadClipper->on = FALSE;
//         return;
//     }
//     m_endThreadClipper->on = TRUE;

//     auto threadNormalOpt = getThreadNormal(pcThread);
//     if (!threadNormalOpt.has_value()){
//         return;
//     }
//     gp_Dir threadNormalAxis = *threadNormalOpt;

//     auto threadOriginOpt = getThreadOrigin(pcThread);
//     if (!threadOriginOpt.has_value()) {
//         return;
//     }
//     gp_Pnt threadOriginPnt = *threadOriginOpt;

//     gp_Pnt endPlanePnt = threadOriginPnt.Translated(
//         gp_Vec(threadNormalAxis) * -pcThread->ThreadDepth.getValue()
//     );

//     SbVec3f endPlanePoint = Base::convertTo<SbVec3f>(endPlanePnt);
//     SbVec3f endPlaneNormal = Base::convertTo<SbVec3f>(threadNormalAxis);

//     // Update the end thread clipper plane
//     m_endThreadClipper->plane.setValue(SbPlane(endPlaneNormal, endPlanePoint));
// }

void ViewProviderThread::updateThreadClipper(const PartDesign::Thread* pcThread)
{
    if (DEBUG) Base::Console().message("[updateThreadClipper]: Starting thread clipper update...\n");

    if (!pcThread) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: Thread object is null -> Aborting\n");
        return;
    }

    if (pcThread->isRecomputing()) {
        if (DEBUG) Base::Console().message("[updateThreadClipper]: Thread is currently recomputing -> Aborting\n");
        return;
    }

    if (!m_endThreadClipper) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: m_endThreadClipper is null -> Aborting\n");
        return;
    }

    std::string theadDepthType;
    try {
        theadDepthType = pcThread->ThreadDepthType.getValueAsString();
        if (DEBUG) Base::Console().message("[updateThreadClipper]: ThreadDepthType value = '%s'\n", theadDepthType.c_str());
    }
    catch (const std::exception& e) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: Failed to read ThreadDepthType enum: %s -> Disabling clipper\n", e.what());
        m_endThreadClipper->on = FALSE;
        return;
    }
    catch (...) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: Unknown exception while reading ThreadDepthType enum -> Disabling clipper\n");
        m_endThreadClipper->on = FALSE;
        return;
    }

    if (theadDepthType == "Hole depth") {
        if (DEBUG) Base::Console().message("[updateThreadClipper]: Depth type is 'Hole depth' -> Turning clipper OFF\n");
        m_endThreadClipper->on = FALSE;
        return;
    }

    m_endThreadClipper->on = TRUE;

    auto threadNormalOpt = getThreadNormal(pcThread);
    if (!threadNormalOpt.has_value()) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: Failed to retrieve thread normal axis -> Aborting\n");
        return;
    }
    gp_Dir threadNormalAxis = *threadNormalOpt;

    auto threadOriginOpt = getThreadOrigin(pcThread);
    if (!threadOriginOpt.has_value()) {
        if (DEBUG) Base::Console().warning("[updateThreadClipper]: Failed to retrieve thread origin -> Aborting\n");
        return;
    }
    gp_Pnt threadOriginPnt = *threadOriginOpt;

    double threadDepthValue = pcThread->ThreadDepth.getValue();
    if (DEBUG) Base::Console().message("[updateThreadClipper]: Calculating clipping plane with depth = %.3f...\n", threadDepthValue);

    gp_Pnt endPlanePnt = threadOriginPnt.Translated(
        gp_Vec(threadNormalAxis) * -threadDepthValue
    );

    SbVec3f endPlanePoint = Base::convertTo<SbVec3f>(endPlanePnt);
    SbVec3f endPlaneNormal = Base::convertTo<SbVec3f>(threadNormalAxis);

    // Update the end thread clipper plane
    m_endThreadClipper->plane.setValue(SbPlane(endPlaneNormal, endPlanePoint));
    if (DEBUG) Base::Console().message("[updateThreadClipper]: Clipper plane updated successfully (OFF at depth limit)\n");
}

std::optional<gp_Dir> ViewProviderThread::getThreadNormal(const PartDesign::Thread* pcThread) const
{
    return pcThread ? pcThread->getThreadNormal() : std::nullopt;
}

std::optional<gp_Pnt> ViewProviderThread::getThreadOrigin(const PartDesign::Thread* pcThread) const
{
    return pcThread ? pcThread->getThreadOrigin() : std::nullopt;
}

std::vector<gp_Pnt> ViewProviderThread::getThreadLocations(const PartDesign::Thread* pcThread) const
{
    if (!pcThread) {
        return {};
    }
    return pcThread->getThreadLocations();
}

std::vector<TopoDS_Face> ViewProviderThread::collectBoreFaces(const PartDesign::Thread* pcThread) const
{
    if (DEBUG) Base::Console().message("[collectBoreFaces]: Starting bore faces collection...\n");

    std::vector<TopoDS_Face> boreFaces;
    if (!pcThread) {
        if (DEBUG) Base::Console().warning("[collectBoreFaces]: Thread object is null -> Empty list\n");
        return boreFaces;
    }

    TopoDS_Shape bodyShape = getCurrentlyVisibleShape(pcThread);
    if (bodyShape.IsNull()) {
        if (DEBUG) Base::Console().warning("[collectBoreFaces]: Visible body shape is null for thread '%s' -> Empty list\n", pcThread->getNameInDocument());
        return boreFaces;
    }

    auto threadNormalOpt = getThreadNormal(pcThread);
    if (!threadNormalOpt.has_value()) {
        if (DEBUG) Base::Console().warning("[collectBoreFaces]: Failed to get thread normal axis -> Empty list\n");
        return boreFaces;
    }
    gp_Dir threadAxis = *threadNormalOpt;

    std::vector<gp_Pnt> validLocations = getThreadLocations(pcThread);
    if (validLocations.empty()) {
        if (DEBUG) Base::Console().warning("[collectBoreFaces]: No valid locations found for thread '%s' -> Empty list\n", pcThread->getNameInDocument());
        return boreFaces;
    }

    const double holeRadius = pcThread->Diameter.getValue() / 2.0; //TODO:get real diameter
    const double distTolerance = 2 * Precision::Confusion();
    const bool isTapered = pcThread->Tapered.getValue();
    const double taperSemiAngleRad = isTapered
        ? Base::toRadians(90 - pcThread->TaperedAngle.getValue())
        : 0.0;

    if (DEBUG) Base::Console().message("[collectBoreFaces]: Filtering faces for '%s' (isTapered=%s, targetRadius=%.3f, locationsCount=%zu)...\n",
                            pcThread->getNameInDocument(),
                            isTapered ? "TRUE" : "FALSE",
                            holeRadius,
                            validLocations.size());

    size_t totalFacesExamined = 0;
    size_t rejectedType = 0;
    size_t rejectedGeomProps = 0;
    size_t rejectedLocation = 0;
    size_t rejectedParallelism = 0;

    for (TopExp_Explorer expl(bodyShape, TopAbs_FACE); expl.More(); expl.Next()) {
        totalFacesExamined++;
        const TopoDS_Face& face = TopoDS::Face(expl.Current());
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (surf.IsNull()) {
            continue;
        }

        // Unwrap trimmed surfaces
        if (surf->IsKind(STANDARD_TYPE(Geom_RectangularTrimmedSurface))) {
            surf = Handle(Geom_RectangularTrimmedSurface)::DownCast(surf)->BasisSurface();
        }

        gp_Ax1 axis;
        bool isMatch = false;

        if (!isTapered) {
            if (!surf->IsKind(STANDARD_TYPE(Geom_CylindricalSurface))) {
                rejectedType++;
                continue;
            }
            auto cyl = Handle(Geom_CylindricalSurface)::DownCast(surf);
            if (std::abs(cyl->Radius() - holeRadius) >= Precision::Confusion()) {
                rejectedGeomProps++;
                continue;
            }
            axis = cyl->Axis();
        }
        else {
            if (!surf->IsKind(STANDARD_TYPE(Geom_ConicalSurface))) {
                rejectedType++;
                continue;
            }
            auto con = Handle(Geom_ConicalSurface)::DownCast(surf);
            double angle = std::abs(con->SemiAngle());
            if (std::abs(angle - taperSemiAngleRad) >= Precision::Angular()) {
                rejectedGeomProps++;
                continue;
            }
            axis = con->Axis();
        }

        for (const auto& loc : validLocations) {
            if (gp_Lin(axis).Distance(loc) < distTolerance) {
                isMatch = true;
                break;
            }
        }

        if (!isMatch) {
            rejectedLocation++;
            continue;
        }

        if (!axis.Direction().IsParallel(threadAxis, Precision::Angular())) {
            rejectedParallelism++;
            continue;
        }

        boreFaces.push_back(face);
    }

    if (boreFaces.empty()) {
        if (DEBUG) Base::Console().warning("[collectBoreFaces]: Examined %zu faces but found NO matching bore faces! "
                                "(Rejected: Type=%zu, Radius/Angle=%zu, Location=%zu, Parallelism=%zu)\n",
                                totalFacesExamined, rejectedType, rejectedGeomProps, rejectedLocation, rejectedParallelism);
    } else {
        if (DEBUG) Base::Console().message("[collectBoreFaces]: Successfully collected %zu bore faces out of %zu examined faces\n",
                                boreFaces.size(), totalFacesExamined);
    }

    return boreFaces;
}

App::Material ViewProviderThread::getGlobalMaterial()
{
    if (DEBUG) Base::Console().message("[getGlobalMaterial]: Fetching global material...\n");

    if (auto* materialProp = dynamic_cast<App::PropertyMaterial*>(getPropertyByName("Material"))) {
        if (DEBUG) Base::Console().message("[getGlobalMaterial]: Found material directly on Thread ViewProvider\n");
        return materialProp->getValue();
    }
    else {
        if (DEBUG) Base::Console().message("[getGlobalMaterial]: Material property not found on Thread ViewProvider\n");
    }

    if (auto* bodyVp = getBodyViewProvider()) {
        if (DEBUG) Base::Console().message("[getGlobalMaterial]: Found parent Body ViewProvider, checking material...\n");
        if (auto* materialProp
            = freecad_cast<App::PropertyMaterial*>(bodyVp->getPropertyByName("Material"))) {
            if (DEBUG) Base::Console().message("[getGlobalMaterial]: Found material on parent Body ViewProvider\n");
            return materialProp->getValue();
        }
        else {
            if (DEBUG) Base::Console().message("[getGlobalMaterial]: Material property not found on parent Body ViewProvider\n");
        }
    }
    else {
        if (DEBUG) Base::Console().warning("[getGlobalMaterial]: Body ViewProvider is NULL!\n");
    }

    if (DEBUG) Base::Console().message("[getGlobalMaterial]: Falling back to App::Material::getDefaultAppearance()\n");
    return App::Material::getDefaultAppearance();
}

TopoDS_Shape ViewProviderThread::getCurrentlyVisibleShape(const PartDesign::Thread* pcThread) const
{
    auto* body = PartDesign::Body::findBodyOf(pcThread);
    if (!body) {
        return {};
    }
    const auto& features = body->Group.getValues();
    auto threadIt = std::ranges::find(features, pcThread);
    if (threadIt == features.end()) {
        return {};
    }
    for (auto it = threadIt; it != features.end(); ++it) {
        auto* posteriorFeature = dynamic_cast<PartDesign::Feature*>(*it);
        if (posteriorFeature && posteriorFeature->Visibility.getValue()) {
            return posteriorFeature->Shape.getValue();
        }
    }
    return body->Shape.getValue();
}

std::pair<gp_Dir, gp_Dir> ViewProviderThread::buildOrthonormalFrame(const gp_Dir& axis)
{
    gp_Dir ref(0, 0, 1);
    if (axis.IsParallel(ref, Precision::Angular())) {
        ref = gp_Dir(0, 1, 0);
    }
    gp_Vec x_vec = axis.Crossed(ref);
    if (x_vec.SquareMagnitude() < Precision::Confusion()) {
        ref = gp_Dir(1, 0, 0);
        x_vec = axis.Crossed(ref);
    }
    gp_Dir x_dir(x_vec);
    gp_Dir y_dir(axis.Crossed(x_dir));
    return {x_dir, y_dir};
}

SbVec2f ViewProviderThread::addVertex(
    std::vector<SbVec3f>& vertices,
    std::vector<SbVec3f>& normals,
    const gp_Pnt& pt,
    const gp_Pnt& origin,
    const gp_Dir& axis,
    const gp_Dir& x_dir,
    const gp_Dir& y_dir,
    double minProj,
    double initialRadius,
    double threadPitch
)
{
    gp_Vec toPoint(origin, pt);
    gp_Vec radialComp = toPoint - (toPoint.Dot(axis) * axis);
    double axialDist = toPoint.Dot(axis) - minProj;
    double currentRadius = radialComp.Magnitude();
    double radialOffset = currentRadius - initialRadius;
    double lengthAlongTaper = std::sqrt((axialDist * axialDist) + (radialOffset * radialOffset));

    float vCoord = static_cast<float>(lengthAlongTaper / threadPitch);
    double angleRad = std::atan2(radialComp.Dot(y_dir), radialComp.Dot(x_dir));
    float uCoord = static_cast<float>(angleRad / (2 * M_PI));
    uCoord -= std::floor(uCoord);

    vertices.emplace_back(pt.X(), pt.Y(), pt.Z());
    gp_Dir normalDir = (radialComp.SquareMagnitude() > std::pow(Precision::Confusion(), 2))
        ? gp_Dir(radialComp)
        : axis;
    normals.emplace_back(normalDir.X(), normalDir.Y(), normalDir.Z());

    return SbVec2f(uCoord, vCoord);
}

namespace
{
Handle(Geom_Surface) unwrapSurface(const TopoDS_Face& face)
{
    Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
    if (!surf.IsNull() && surf->IsKind(STANDARD_TYPE(Geom_RectangularTrimmedSurface))) {
        surf = Handle(Geom_RectangularTrimmedSurface)::DownCast(surf)->BasisSurface();
    }
    return surf;
}
}  // namespace

void ViewProviderThread::handleSeamTriangle(
    std::vector<SbVec3f>& vertices,
    std::vector<SbVec3f>& normals,
    std::vector<SbVec2f>& uvs,
    std::array<int, 3>& triIndices
)
{
    constexpr float seamThreshold = 0.5F;

    bool crossesSeam = std::abs(uvs[triIndices[0]][0] - uvs[triIndices[1]][0]) > seamThreshold
        || std::abs(uvs[triIndices[1]][0] - uvs[triIndices[2]][0]) > seamThreshold
        || std::abs(uvs[triIndices[2]][0] - uvs[triIndices[0]][0]) > seamThreshold;

    if (!crossesSeam) {
        return;
    }

    int idx0 = triIndices[0];
    int idx1 = triIndices[1];
    int idx2 = triIndices[2];

    if (uvs[idx0][0] < seamThreshold) {
        SbVec2f uv = uvs[idx0];
        uv[0] += 1.0F;
        int newIdx = static_cast<int>(vertices.size());
        vertices.push_back(vertices[idx0]);
        normals.push_back(normals[idx0]);
        uvs.push_back(uv);
        triIndices[0] = newIdx;
    }

    if (uvs[idx1][0] < seamThreshold) {
        SbVec2f uv = uvs[idx1];
        uv[0] += 1.0F;
        int newIdx = static_cast<int>(vertices.size());
        vertices.push_back(vertices[idx1]);
        normals.push_back(normals[idx1]);
        uvs.push_back(uv);
        triIndices[1] = newIdx;
    }

    if (uvs[idx2][0] < seamThreshold) {
        SbVec2f uv = uvs[idx2];
        uv[0] += 1.0F;
        int newIdx = static_cast<int>(vertices.size());
        vertices.push_back(vertices[idx2]);
        normals.push_back(normals[idx2]);
        uvs.push_back(uv);
        triIndices[2] = newIdx;
    }
}

bool ViewProviderThread::generateBoreMeshData(
    const PartDesign::Thread* pcThread,
    const gp_Pnt& threadOriginPnt,
    std::vector<SbVec3f>& vertices,
    std::vector<SbVec3f>& normals,
    std::vector<int>& indices,
    std::vector<SbVec2f>& uvs
)
{
    if (DEBUG) Base::Console().message("[generateBoreMeshData]: Starting mesh generation...\n");

    const double threadPitch = pcThread->getThreadPitch();
    if (threadPitch == 0.0) {
        if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Thread pitch is 0.0 -> FALSE\n");
        return false;
    }

    vertices.clear();
    normals.clear();
    indices.clear();
    uvs.clear();

    const auto& boreFaces = collectBoreFaces(pcThread);
    if (boreFaces.empty()) {
        if (DEBUG) Base::Console().warning("[generateBoreMeshData]: No bore faces collected for thread '%s' -> FALSE\n", pcThread->getNameInDocument());
        return false;
    }

    if (DEBUG) Base::Console().message("[generateBoreMeshData]: Collected %zu bore faces\n", boreFaces.size());

    auto threadNormalOpt = getThreadNormal(pcThread);
    if (!threadNormalOpt.has_value()) {
        if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Failed to get thread normal axis -> FALSE\n");
        return false;
    }
    gp_Dir threadNormalAxis = *threadNormalOpt;

    double minProj = std::numeric_limits<double>::max();
    double maxProj = std::numeric_limits<double>::lowest();
    size_t totalTriangulatedPoints = 0;

    // --- Compute projection bounds ---
    for (size_t i = 0; i < boreFaces.size(); ++i) {
        std::vector<gp_Pnt> meshPoints;
        std::vector<Poly_Triangle> meshFacets;
        if (Part::Tools::getTriangulation(boreFaces[i], meshPoints, meshFacets)) {
            totalTriangulatedPoints += meshPoints.size();
            for (const auto& p : meshPoints) {
                double proj = gp_Vec(threadOriginPnt, p).Dot(threadNormalAxis);
                minProj = std::min(minProj, proj);
                maxProj = std::max(maxProj, proj);
            }
        } else {
            if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Triangulation failed for bore face index %zu\n", i);
        }
    }

    if (totalTriangulatedPoints == 0) {
        if (DEBUG) Base::Console().warning("[generateBoreMeshData]: All face triangulations produced 0 points -> FALSE\n");
        return false;
    }

    if (DEBUG) Base::Console().message("[generateBoreMeshData]: Projection bounds computed (minProj: %.3f, maxProj: %.3f, totalPoints: %zu)\n", 
                            minProj, maxProj, totalTriangulatedPoints);

    const double threadRadius = pcThread->Diameter.getValue() / 2.0;
    const double coneSemiAngleRad = pcThread->Tapered.getValue()
        ? Base::toRadians(pcThread->TaperedAngle.getValue() * 0.5)
        : 0.0;
    const double initialRadius = (minProj * std::tan(coneSemiAngleRad)) + threadRadius;

    bool success = false;
    size_t skippedFacesCount = 0;

    for (size_t i = 0; i < boreFaces.size(); ++i) {
        const auto& face = boreFaces[i];
        std::vector<gp_Pnt> meshPoints;
        std::vector<Poly_Triangle> meshFacets;
        if (!Part::Tools::getTriangulation(face, meshPoints, meshFacets)) {
            if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Face index %zu skipped (triangulation failed)\n", i);
            skippedFacesCount++;
            continue;
        }

        Handle(Geom_Surface) surf = unwrapSurface(face);
        gp_Ax3 surfPos;
        if (auto cyl = Handle(Geom_CylindricalSurface)::DownCast(surf)) {
            surfPos = cyl->Position();
        }
        else if (auto cone = Handle(Geom_ConicalSurface)::DownCast(surf)) {
            surfPos = cone->Position();
        }
        else {
            if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Face index %zu skipped (Surface is neither Cylindrical nor Conical)\n", i);
            skippedFacesCount++;
            continue;
        }

        auto [x_dir, y_dir] = buildOrthonormalFrame(surfPos.Direction());
        gp_Pnt localOrigin = surfPos.Location();

        std::vector<int> localToGlobalIndex(meshPoints.size());
        for (size_t pIdx = 0; pIdx < meshPoints.size(); ++pIdx) {
            localToGlobalIndex[pIdx] = static_cast<int>(vertices.size());
            uvs.push_back(addVertex(
                vertices,
                normals,
                meshPoints[pIdx],
                localOrigin,
                surfPos.Direction(),
                x_dir,
                y_dir,
                minProj,
                initialRadius,
                threadPitch
            ));
        }

        // --- Build indices ---
        for (const auto& facet : meshFacets) {
            std::array<int, 3> n = {1, 1, 1};
            facet.Get(n[0], n[1], n[2]);
            std::array<int, 3> triIndices
                = {localToGlobalIndex[n[0]], localToGlobalIndex[n[1]], localToGlobalIndex[n[2]]};
            handleSeamTriangle(vertices, normals, uvs, triIndices);

            indices.insert(indices.end(), {triIndices[0], triIndices[1], triIndices[2], -1});
        }
        
        success = true;
    }

    if (!success) {
        if (DEBUG) Base::Console().warning("[generateBoreMeshData]: Failed to process any bore faces (Skipped %zu/%zu faces) -> FALSE\n", 
                                skippedFacesCount, boreFaces.size());
    } else {
        if (DEBUG) Base::Console().message("[generateBoreMeshData]: Mesh generated successfully (Verts: %zu, Norms: %zu, Inds: %zu, UVs: %zu)\n",
                                vertices.size(), normals.size(), indices.size(), uvs.size());
    }

    return success;
}

bool ViewProviderThread::isHoleThreadVisible() const
{
    if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Checking thread visibility status...\n");

    auto* thread = getObject<PartDesign::Thread>();
    if (!thread) {
        if (DEBUG) Base::Console().warning("[isHoleThreadVisible]: Thread object is null -> FALSE\n");
        return false;
    }

    auto* body = PartDesign::Body::findBodyOf(thread);
    if (!body) {
        if (DEBUG) Base::Console().warning("[isHoleThreadVisible]: Parent Body not found for thread '%s' -> FALSE\n", thread->getNameInDocument());
        return false;
    }

    // --- Checagem de Visibilidade e Flags do Objeto ---
    bool isBodyVisible = body->Visibility.getValue();
    bool isSuppressed = thread->Suppressed.getValue();
    bool isThreaded = thread->Threaded.getValue();
    bool isCosmetic = thread->CosmeticThread.getValue();
    bool isModel = thread->ModelThread.getValue();

    if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Conditions for '%s': BodyVis=%s, Suppressed=%s, Threaded=%s, Cosmetic=%s, Model=%s\n",
                            thread->getNameInDocument(),
                            isBodyVisible ? "TRUE" : "FALSE",
                            isSuppressed ? "TRUE" : "FALSE",
                            isThreaded ? "TRUE" : "FALSE",
                            isCosmetic ? "TRUE" : "FALSE",
                            isModel ? "TRUE" : "FALSE");

    if (!isBodyVisible || isSuppressed || !isThreaded || !isCosmetic || isModel) {
        if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Initial condition check failed -> FALSE\n");
        return false;
    }

    // --- Verificação na árvore de recursos do Body ---
    const auto& features = body->Group.getValues();
    auto threadIt = std::ranges::find(features, thread);
    if (threadIt == features.end()) {
        if (DEBUG) Base::Console().warning("[isHoleThreadVisible]: Thread '%s' not found inside parent Body group -> FALSE\n", thread->getNameInDocument());
        return false;
    }

    if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Checking posterior features visibility starting from thread '%s'...\n", thread->getNameInDocument());

    for (auto it = threadIt; it != features.end(); ++it) {
        auto* posteriorFeature = dynamic_cast<PartDesign::Feature*>(*it);
        if (posteriorFeature) {
            bool featureVis = posteriorFeature->Visibility.getValue();
            if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Feature '%s' visibility = %s\n",
                                    posteriorFeature->getNameInDocument(),
                                    featureVis ? "TRUE" : "FALSE");

            if (featureVis) {
                if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Visible posterior feature found ('%s') -> TRUE\n", posteriorFeature->getNameInDocument());
                return true;
            }
        }
    }

    // Chegou ao fim e nenhuma feature posterior está visível
    if (DEBUG) Base::Console().message("[isHoleThreadVisible]: Reached end of features list with no visible posterior feature -> FALSE\n");
    return false;
}

void ViewProviderThread::updateOverlay()
{
    if (DEBUG) Base::Console().message("[updateOverlay]: Starting overlay update\n");

    auto* thread = getObject<PartDesign::Thread>();
    if (!thread) {
        if (DEBUG) Base::Console().warning("[updateOverlay]: Thread object is null, aborting\n");
        return;
    }

    if (DEBUG) Base::Console().message("[updateOverlay]: Processing thread object '%s'\n", thread->getNameInDocument());

    bool isThreadVisible = isHoleThreadVisible();
    if (DEBUG) Base::Console().message("[updateOverlay]: Thread visibility status = %s\n", isThreadVisible ? "TRUE" : "FALSE");

    auto* bodyVp = getBodyViewProvider();
    if (!bodyVp) {
        if (DEBUG) Base::Console().warning("[updateOverlay]: Body view provider is null, aborting\n");
        return;
    }

    // --- Cleanup ---
    auto it = m_threadOverlays.find(thread);
    if (it != m_threadOverlays.end()) {
        if (DEBUG) Base::Console().message("[updateOverlay]: Cleaning up existing overlay for thread '%s'\n", thread->getNameInDocument());
        SoSwitch* existingSwitch = it->second;
        
        if (bodyVp->getRoot()) {
            bodyVp->getRoot()->removeChild(existingSwitch);
            if (DEBUG) Base::Console().message("[updateOverlay]: Existing SoSwitch removed from scene graph root\n");
        } else {
            if (DEBUG) Base::Console().warning("[updateOverlay]: Scene graph root was null during cleanup\n");
        }

        existingSwitch->unref();
        m_threadOverlays.erase(it);
        if (DEBUG) Base::Console().message("[updateOverlay]: Cleanup complete and entry erased from map\n");
    } else {
        if (DEBUG) Base::Console().message("[updateOverlay]: No existing overlay found in map to clean up\n");
    }

    // --- Add the cosmetic thread overlay ---
    if (isThreadVisible) {
        if (DEBUG) Base::Console().message("[updateOverlay]: Generating new cosmetic thread texture separator... HEHE\n");
        if (SoSeparator* newSep = createThreadTextureSeparator()) {
            if (DEBUG) Base::Console().message("[updateOverlay]: Texture separator created successfully (ptr:)\n");
            
            auto* threadSwitch = new SoSwitch();
            threadSwitch->ref();
            threadSwitch->addChild(newSep);

            if (bodyVp->getRoot()) {
                bodyVp->getRoot()->addChild(threadSwitch);
                threadSwitch->whichChild = SO_SWITCH_ALL;
                m_threadOverlays[thread] = threadSwitch;
                if (DEBUG) Base::Console().message("[updateOverlay]: New cosmetic thread successfully attached to scene graph (SoSwitch ptr:)\n");
            } else {
                if (DEBUG) Base::Console().error("[updateOverlay]: Failed to attach SoSwitch — scene graph root is null\n");
                threadSwitch->unref();
            }
        } else {
            if (DEBUG) Base::Console().warning("[updateOverlay]: Failed to create texture separator (createThreadTextureSeparator returned null)\n");
        }
    } else {
        if (DEBUG) Base::Console().message("[updateOverlay]: Overlay skipped because thread is not visible\n");
    }

    if (DEBUG) Base::Console().message("[updateOverlay]: Overlay update finished\n");
}
