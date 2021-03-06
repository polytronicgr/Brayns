/* Copyright (c) 2020, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Nadir Roman Guerrero <nadir.romanguerrero@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "PBRVolumesPlugin.h"
#include "Log.h"

#include <brayns/common/ActionInterface.h>
#include <brayns/common/PropertyMap.h>

#include <brayns/common/geometry/TriangleMesh.h>

#include <brayns/engine/Engine.h>
#include <brayns/engine/Model.h>
#include <brayns/engine/Scene.h>

#include <brayns/pluginapi/PluginAPI.h>

template<typename Request>
inline bool handleCommonParams(const Request& req, brayns::Message& resp)
{
    if(req.absorption.size() < 3)
    {
        resp.setError(2, "Absroption coefficent requires 3 float numbers");
        return false;
    }

    if(req.scattering.size() < 3)
    {
        resp.setError(3, "Scattering coefficent requires 3 float numbers");
        return false;
    }

    if(req.Le.size() < 3)
    {
        resp.setError(4, "Le parameter requires 3 float numbers");
        return false;
    }

    return true;
}

template<typename Request>
inline bool handleBoxParams(const Request& req, brayns::Message& resp)
{
    if(req.p0.size() < 3)
    {
        resp.setError(5, "Minimum bound (p0) parameter requires 3 floats");
        return false;
    }

    if(req.p1.size() < 3)
    {
        resp.setError(6, "Maximum bound (p1) parameter requires 3 floats");
        return false;
    }

    return true;
}

template<typename Request>
inline void addCommonParams(const Request& req, brayns::PropertyMap& metaObject)
{
    const std::array<double, 3> absoption = {req.absorption[0],
                                             req.absorption[1],
                                             req.absorption[2]};
    metaObject.setProperty({"sig_a", absoption});

    const std::array<double, 3> scattering = {req.scattering[0],
                                              req.scattering[1],
                                              req.scattering[2]};
    metaObject.setProperty({"sig_s", scattering});

    metaObject.setProperty({"scale", req.scale});
    metaObject.setProperty({"g", req.g});

    const std::array<double, 3> Le = {req.Le[0],
                                      req.Le[1],
                                      req.Le[2]};
    metaObject.setProperty({"Le", Le});
}

template<typename Request>
inline void createBoxMesh(const Request& req, brayns::Model& model)
{
    const brayns::Vector3f min (req.p0[0], req.p0[1], req.p0[2]);
    const brayns::Vector3f max (req.p1[0], req.p1[1], req.p1[2]);
    model.getTriangleMeshes()[brayns::NO_MATERIAL] = brayns::createBox(min, max);
}

PBRVolumesPlugin::PBRVolumesPlugin()
 : brayns::ExtensionPlugin()
{
}

PBRVolumesPlugin::~PBRVolumesPlugin()
{
}

void PBRVolumesPlugin::init()
{
    auto actionInterface = _api->getActionInterface();
    if(actionInterface)
    {
        actionInterface->registerRequest<AddHomogeneusVolume, brayns::Message>(
            {"add-homogeneus-volume",
             "Adds a constant density volume to the scene",
             "AddHomogeneusVolume",
             "Parameters to define the homogeneus volume"},
             [&](const AddHomogeneusVolume& request){
                return _addHomogeneusVolume(request);
        });

        actionInterface->registerRequest<AddHomogeneusVolumeToModel, brayns::Message>(
            {"add-homogeneus-volume-model",
             "Adds a constant density volume to an already loaded model in the scene",
             "AddHomogeneusVolumeToModel",
             "Parameters to define the homogeneus volume, and the model to add it to"},
            [&](const AddHomogeneusVolumeToModel& request){
                return _addHomogeneusVolumeToModel(request);
        });

        actionInterface->registerRequest<AddHeterogeneusVolume, brayns::Message>(
            {"add-heterogeneus-volume",
             "Adds a non constant density volume to the scene",
             "AddHeterogeneusVolume",
             "Parameters to define the heterogeneus volume"},
            [&](const AddHeterogeneusVolume& request){
                return _addHeterogeneusVolume(request);
        });

        actionInterface->registerRequest<AddGridVolume, brayns::Message>(
            {"add-grid-volume",
             "Adds a grid based volume to the scene, in which each cell has its own density",
             "AddGridVolume",
             "Parameters to define the grid and the volume inside it"},
            [&](const AddGridVolume& request){
                return _addGridVolume(request);
        });
    }
}

brayns::Message PBRVolumesPlugin::_addHomogeneusVolume(const AddHomogeneusVolume& req)
{
    brayns::Message resp;

    if(!handleCommonParams(req, resp))
        return resp;

    if(!handleBoxParams(req, resp))
        return resp;

    brayns::PropertyMap metaObject;

    metaObject.setProperty({"volume_type", std::string("homogeneus")});
    metaObject.setProperty({"density", req.density});
    addCommonParams(req, metaObject);

    auto modelPtr = _api->getScene().createModel();

    createBoxMesh(req, *modelPtr);

    modelPtr->addMetaObject(brayns::NO_MATERIAL, metaObject);
    const std::string name = req.name.empty()? "Volume" : req.name;
    _api->getScene().addModel(std::make_shared<brayns::ModelDescriptor>(std::move(modelPtr),
                                                                        name));
    _api->getScene().markModified();
    _api->getEngine().triggerRender();

    return resp;
}

brayns::Message
PBRVolumesPlugin::_addHomogeneusVolumeToModel(const AddHomogeneusVolumeToModel& req)
{
    brayns::Message resp;

    if(!handleCommonParams(req, resp))
        return resp;

    const size_t modelId = static_cast<size_t>(req.modelId);
    auto modelPtr = _api->getScene().getModel(modelId);
    if(!modelPtr)
    {
        resp.setError(5, "The given model ID does not exist");
        return resp;
    }

    brayns::PropertyMap metaObject;
    metaObject.setProperty({"volume_type", std::string("homogeneus")});
    metaObject.setProperty({"density", req.density});
    addCommonParams(req, metaObject);

    modelPtr->getModel().addMetaObject(brayns::NO_MATERIAL, metaObject);
    modelPtr->markModified();
    _api->getScene().markModified();
    _api->getEngine().triggerRender();

    return resp;
}

brayns::Message PBRVolumesPlugin::_addHeterogeneusVolume(const AddHeterogeneusVolume& req)
{
    brayns::Message resp;

    if(!handleCommonParams(req, resp))
        return resp;

    if(!handleBoxParams(req, resp))
        return resp;

    brayns::PropertyMap metaObject;

    metaObject.setProperty({"volume_type", std::string("heterogeneus")});
    metaObject.setProperty({"min_density", req.minDensity});
    metaObject.setProperty({"max_density", req.maxDensity});
    addCommonParams(req, metaObject);

    auto modelPtr = _api->getScene().createModel();

    createBoxMesh(req, *modelPtr);

    modelPtr->addMetaObject(brayns::NO_MATERIAL, metaObject);
    const std::string name = req.name.empty()? "Volume" : req.name;
    _api->getScene().addModel(std::make_shared<brayns::ModelDescriptor>(std::move(modelPtr),
                                                                        name));
    _api->getScene().markModified();
    _api->getEngine().triggerRender();

    return resp;
}

brayns::Message PBRVolumesPlugin::_addGridVolume(const AddGridVolume& req)
{
    brayns::Message resp;

    if(!handleCommonParams(req, resp))
        return resp;

    if(!handleBoxParams(req, resp))
        return resp;

    if(req.nx < 1 || req.ny < 1 || req.nz < 1)
    {
        resp.setError(7, "The grid dimensions must be positive non zero values");
        return resp;
    }

    if(static_cast<size_t>(req.nx * req.ny * req.nz) != req.density.size())
    {
        resp.setError(8, "The grid dimensions (nx x ny x nz) must match the number of denisty"
                         " values");
        return resp;
    }

    brayns::PropertyMap metaObject;

    metaObject.setProperty({"volume_type", std::string("grid")});
    metaObject.setProperty({"grid_type", req.gridType});
    metaObject.setProperty({"density", req.density});
    metaObject.setProperty({"nx", req.nx});
    metaObject.setProperty({"ny", req.ny});
    metaObject.setProperty({"nz", req.nz});
    addCommonParams(req, metaObject);

    auto modelPtr = _api->getScene().createModel();

    createBoxMesh(req, *modelPtr);

    modelPtr->addMetaObject(brayns::NO_MATERIAL, metaObject);
    const std::string name = req.name.empty()? "Volume" : req.name;
    _api->getScene().addModel(std::make_shared<brayns::ModelDescriptor>(std::move(modelPtr),
                                                                        name));
    _api->getScene().markModified();
    _api->getEngine().triggerRender();

    return resp;
}

extern "C" brayns::ExtensionPlugin* brayns_plugin_create(int /*argc*/,
                                                         char** /*argv*/)
{
    PLUGIN_INFO << "Initializing Physically Based Volumes plugin" << std::endl;
    return new PBRVolumesPlugin();
}
