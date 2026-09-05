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

#pragma once

#include "ViewProviderDressUp.h"
#include <Gui/ViewProviderTextureExtension.h>
#include <App/Material.h>

#include <vector>
#include <memory>

class SoTexture2Transform;
class SoSeparator;
class SoClipPlane;
class QMenu;

namespace App
{
class DocumentObject;
class Property;
}  // namespace App

namespace PartDesign
{
// class Hole;
class Thread;
}

// Forward declarations for OpenCascade classes
class TopoDS_Face;
class TopoDS_Shape;
class gp_Dir;
class gp_Pnt;


namespace PartDesignGui
{

class PartDesignGuiExport ViewProviderThread: public ViewProviderDressUp
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderThread);

    SoClipPlane* m_endThreadClipper {nullptr};
    SoTexture2Transform* m_textureTransform {nullptr};

public:
    /// constructor
    ViewProviderThread();
    
     /// destructor
    // ~ViewProviderThread() override;
    // bool onDelete(const std::vector<std::string>& arg) override;

    /// grouping handling
    // std::vector<App::DocumentObject*> claimChildren() const override;
    const std::string& featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;
    SoSeparator* createThreadTextureSeparator();
    bool isHoleThreadVisible() const;
    void updateOverlay() override;

protected:
    /// Returns a newly create dialog for the part to be placed in the task view
    TaskDlgFeatureParameters* getEditDialog() override;
    void updateData(const App::Property* prop) override;

private:
    std::unique_ptr<Gui::ViewProviderTextureExtension> textureExtension;
    std::optional<gp_Dir> getThreadNormal(const PartDesign::Thread* pcThread) const;
    std::optional<gp_Pnt> getThreadOrigin(const PartDesign::Thread* pcThread) const;
    std::vector<gp_Pnt> getThreadLocations(const PartDesign::Thread* pcThread) const;
    App::Material getGlobalMaterial();
    TopoDS_Shape getCurrentlyVisibleShape(const PartDesign::Thread* pcThread) const;
    void updateThreadClipper(const PartDesign::Thread* pcThread);
    void updateThreadDirection(const PartDesign::Thread* pcThread);
    void applyThreadPhaseOffset(const PartDesign::Thread* pcThread);

    // meshing and UVs
    std::vector<TopoDS_Face> collectBoreFaces(const PartDesign::Thread* pcThread) const;
    bool generateBoreMeshData(
        const PartDesign::Thread* pcThread,
        const gp_Pnt& threadOriginPnt,
        std::vector<SbVec3f>& vertices,
        std::vector<SbVec3f>& normals,
        std::vector<int>& indices,
        std::vector<SbVec2f>& uvs
    );
    std::pair<gp_Dir, gp_Dir> buildOrthonormalFrame(const gp_Dir& axis);
    SbVec2f addVertex(
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
    );
    void handleSeamTriangle(
        std::vector<SbVec3f>& vertices,
        std::vector<SbVec3f>& normals,
        std::vector<SbVec2f>& uvs,
        std::array<int, 3>& triIndices
    );
    std::map<const PartDesign::Thread*, SoSwitch*> m_threadOverlays;
    void clearThreadTextures();
};

}  // namespace PartDesignGui
