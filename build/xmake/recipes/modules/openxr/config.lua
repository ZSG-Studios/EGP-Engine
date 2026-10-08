-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy((R.contains({"linuxbsd", "windows", "android", "macos"}, platform))) then
        return not R.truthy(R.index(env, "disable_xr"))
    else
        return false
    end
end
function configure(env)
end
function get_doc_classes()
    return {"OpenXRInterface", "OpenXRAction", "OpenXRActionSet", "OpenXRActionMap", "OpenXRAPIExtension", "OpenXRExtensionWrapper", "OpenXRExtensionWrapperExtension", "OpenXRFrameSynthesisExtension", "OpenXRFutureResult", "OpenXRFutureExtension", "OpenXRInteractionProfile", "OpenXRInteractionProfileMetadata", "OpenXRIPBinding", "OpenXRHand", "OpenXRVisibilityMask", "OpenXRFoveatedInsetViewport", "OpenXRCompositionLayer", "OpenXRCompositionLayerQuad", "OpenXRCompositionLayerCylinder", "OpenXRCompositionLayerEquirect", "OpenXRBindingModifier", "OpenXRIPBindingModifier", "OpenXRActionBindingModifier", "OpenXRAnalogThresholdModifier", "OpenXRDpadBindingModifier", "OpenXRInteractionProfileEditorBase", "OpenXRInteractionProfileEditor", "OpenXRBindingModifierEditor", "OpenXRHapticBase", "OpenXRHapticVibration", "OpenXRRenderModelExtension", "OpenXRRenderModel", "OpenXRRenderModelManager", "OpenXRStructureBase", "OpenXRSpatialEntityExtension", "OpenXRSpatialEntityTracker", "OpenXRAnchorTracker", "OpenXRPlaneTracker", "OpenXRMarkerTracker", "OpenXRSpatialCapabilityConfigurationBaseHeader", "OpenXRSpatialCapabilityConfigurationAnchor", "OpenXRSpatialCapabilityConfigurationQrCode", "OpenXRSpatialCapabilityConfigurationMicroQrCode", "OpenXRSpatialCapabilityConfigurationAruco", "OpenXRSpatialCapabilityConfigurationAprilTag", "OpenXRSpatialContextPersistenceConfig", "OpenXRSpatialCapabilityConfigurationPlaneTracking", "OpenXRSpatialComponentData", "OpenXRSpatialComponentBounded2DList", "OpenXRSpatialComponentBounded3DList", "OpenXRSpatialComponentParentList", "OpenXRSpatialComponentMesh2DList", "OpenXRSpatialComponentMesh3DList", "OpenXRSpatialComponentPlaneAlignmentList", "OpenXRSpatialComponentPolygon2DList", "OpenXRSpatialComponentPlaneSemanticLabelList", "OpenXRSpatialComponentMarkerList", "OpenXRSpatialQueryResultData", "OpenXRSpatialComponentAnchorList", "OpenXRSpatialComponentPersistenceList", "OpenXRSpatialAnchorCapability", "OpenXRSpatialPlaneTrackingCapability", "OpenXRSpatialMarkerTrackingCapability", "OpenXRAndroidThreadSettingsExtension", "OpenXRUserPresenceExtension", "OpenXRSpatialContainerExtension", "OpenXRSpatialContainerSelfRenderingExtension", "OpenXRSpatialContainerState"}
end
function get_doc_path()
    return "doc_classes"
end
