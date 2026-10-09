using System;
using System.Runtime.InteropServices;
using Godot.Bridge;

// ReSharper disable InconsistentNaming

namespace Godot.NativeInterop
{
    internal static class InteropUtils
    {
        public static GodotObject UnmanagedGetManaged(IntPtr unmanaged)
        {
            // The native pointer may be null
            if (unmanaged == IntPtr.Zero)
                return null;

            IntPtr gcHandlePtr;
            godot_bool hasCsScriptInstance;

            // First try to get the tied managed instance from a CSharpInstance script instance

            gcHandlePtr = NativeFuncs.godotsharp_internal_unmanaged_get_script_instance_managed(
                unmanaged, out hasCsScriptInstance);

            if (gcHandlePtr != IntPtr.Zero)
                return (GodotObject)GCHandle.FromIntPtr(gcHandlePtr).Target;

            // Otherwise, if the object has a CSharpInstance script instance, return null

            if (hasCsScriptInstance.ToBool())
                return null;

            // If it doesn't have a CSharpInstance script instance, try with native instance bindings

            gcHandlePtr = NativeFuncs.godotsharp_internal_unmanaged_get_instance_binding_managed(unmanaged);

            object target = gcHandlePtr != IntPtr.Zero ? GCHandle.FromIntPtr(gcHandlePtr).Target : null;

            if (target != null)
                return (GodotObject)target;

            // If the native instance binding GC handle target was collected, create a new one

            gcHandlePtr = NativeFuncs.godotsharp_internal_unmanaged_instance_binding_create_managed(
                unmanaged, gcHandlePtr);

            return gcHandlePtr != IntPtr.Zero ? (GodotObject)GCHandle.FromIntPtr(gcHandlePtr).Target : null;
        }

        public static void TieManagedToUnmanaged(GodotObject managed, IntPtr unmanaged,
            StringName nativeName, bool refCounted, Type type, Type nativeType)
        {
            var handle = refCounted ? CustomGCHandle.AllocWeak(managed) : CustomGCHandle.AllocStrong(managed, type);
            bool transferred = false;
            try
            {
                Error tied;
                if (type == nativeType)
                {
                    var nativeNameSelf = (godot_string_name)nativeName.NativeValue;
                    tied = NativeFuncs.godotsharp_internal_tie_native_managed_to_unmanaged(
                        GCHandle.ToIntPtr(handle), unmanaged, nativeNameSelf, refCounted.ToGodotBool());
                }
                else
                {
                    unsafe
                    {
                        // Native always consumes this script Ref. It consumes the
                        // separate handle only when its fallible receipt is OK.
                        godot_ref script;
                        ScriptManagerBridge.GetOrLoadOrCreateScriptForType(type, &script);
                        tied = NativeFuncs.godotsharp_internal_tie_user_managed_to_unmanaged(
                            GCHandle.ToIntPtr(handle), unmanaged, &script, refCounted.ToGodotBool());
                    }
                }
                if (tied != Error.Ok)
                    throw new InvalidOperationException("Native managed Tie failed: " + tied);
                transferred = true;
            }
            finally
            {
                if (!transferred) CustomGCHandle.Free(handle);
            }
        }

        public static void TieManagedToUnmanagedWithPreSetup(GodotObject managed, IntPtr unmanaged,
            Type type, Type nativeType)
        {
            if (type == nativeType) return;
            var handle = CustomGCHandle.AllocStrong(managed);
            bool transferred = false;
            try
            {
                Error tied = NativeFuncs.godotsharp_internal_tie_managed_to_unmanaged_with_pre_setup(
                    GCHandle.ToIntPtr(handle), unmanaged);
                if (tied != Error.Ok)
                    throw new InvalidOperationException("Native managed Tie failed: " + tied);
                transferred = true;
            }
            finally
            {
                if (!transferred) CustomGCHandle.Free(handle);
            }
        }

        public static GodotObject EngineGetSingleton(string name)
        {
            using godot_string src = Marshaling.ConvertStringToNative(name);
            return UnmanagedGetManaged(NativeFuncs.godotsharp_engine_get_singleton(src));
        }
    }
}
