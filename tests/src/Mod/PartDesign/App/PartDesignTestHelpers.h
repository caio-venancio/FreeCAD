// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

namespace PartDesignTestHelpers
{

inline int findEnumIndex(const std::vector<std::string>& enums, const std::string& needle)
{
    for (size_t i = 0; i < enums.size(); ++i) {
        if (enums[i].find(needle) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct ThreadShapeSignature
{
    double volume {0.0};
    double surface {0.0};
    gp_Pnt centerOfMass;
    double xmin {0.0};
    double ymin {0.0};
    double zmin {0.0};
    double xmax {0.0};
    double ymax {0.0};
    double zmax {0.0};
    int faces {0};
    int edges {0};
    int vertices {0};
};

inline ThreadShapeSignature getThreadShapeSignature(const TopoDS_Shape& shape)
{
    GProp_GProps volumeProperties;
    BRepGProp::VolumeProperties(shape, volumeProperties);

    GProp_GProps surfaceProperties;
    BRepGProp::SurfaceProperties(shape, surfaceProperties);

    Bnd_Box box;
    BRepBndLib::Add(shape, box);

    ThreadShapeSignature signature;
    signature.volume = volumeProperties.Mass();
    signature.surface = surfaceProperties.Mass();
    signature.centerOfMass = volumeProperties.CentreOfMass();
    box.Get(
        signature.xmin,
        signature.ymin,
        signature.zmin,
        signature.xmax,
        signature.ymax,
        signature.zmax
    );

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        ++signature.faces;
    }
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        ++signature.edges;
    }
    for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
        ++signature.vertices;
    }
    return signature;
}

}  // namespace PartDesignTestHelpers
