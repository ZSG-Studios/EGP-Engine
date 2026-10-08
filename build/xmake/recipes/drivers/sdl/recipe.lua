-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local driver_obj, env, env_sdl, env_thirdparty, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_sdl = env:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_sdl")) then
        thirdparty_dir = "#thirdparty/sdl/"
        env_sdl:prepend({["CPPDEFINES"] = {"SDL_PLATFORM_PRIVATE"}})
        env_sdl:prepend({["CPPPATH"] = {thirdparty_dir, R.add(thirdparty_dir, "include"), R.add(thirdparty_dir, "include/build_config"), "."}})
        thirdparty_sources = {"SDL.c", "SDL_assert.c", "SDL_error.c", "SDL_guid.c", "SDL_hashtable.c", "SDL_hints.c", "SDL_list.c", "SDL_log.c", "SDL_properties.c", "SDL_utils.c", "atomic/SDL_atomic.c", "atomic/SDL_spinlock.c", "events/SDL_events.c", "events/SDL_eventwatch.c", "haptic/SDL_haptic.c", "io/SDL_iostream.c", "joystick/SDL_gamepad.c", "joystick/SDL_joystick.c", "joystick/SDL_steam_virtual_gamepad.c", "joystick/controller_type.c", "libm/e_atan2.c", "libm/e_exp.c", "libm/e_fmod.c", "libm/e_log.c", "libm/e_log10.c", "libm/e_pow.c", "libm/e_rem_pio2.c", "libm/e_sqrt.c", "libm/k_cos.c", "libm/k_rem_pio2.c", "libm/k_sin.c", "libm/k_tan.c", "libm/s_atan.c", "libm/s_copysign.c", "libm/s_cos.c", "libm/s_fabs.c", "libm/s_floor.c", "libm/s_isinf.c", "libm/s_isinff.c", "libm/s_isnan.c", "libm/s_isnanf.c", "libm/s_modf.c", "libm/s_scalbn.c", "libm/s_sin.c", "libm/s_tan.c", "misc/SDL_libusb.c", "sensor/SDL_sensor.c", "sensor/dummy/SDL_dummysensor.c", "stdlib/SDL_crc16.c", "stdlib/SDL_crc32.c", "stdlib/SDL_getenv.c", "stdlib/SDL_iconv.c", "stdlib/SDL_malloc.c", "stdlib/SDL_memcpy.c", "stdlib/SDL_memmove.c", "stdlib/SDL_memset.c", "stdlib/SDL_mslibc.c", "stdlib/SDL_murmur3.c", "stdlib/SDL_qsort.c", "stdlib/SDL_random.c", "stdlib/SDL_stdlib.c", "stdlib/SDL_string.c", "stdlib/SDL_strtokr.c", "thread/SDL_thread.c", "timer/SDL_timer.c"}
        thirdparty_sources = R.iadd(thirdparty_sources, {"haptic/hidapi/SDL_hidapihaptic_lg4ff.c", "haptic/hidapi/SDL_hidapihaptic.c", "hidapi/SDL_hidapi.c", "joystick/hidapi/SDL_hidapi_8bitdo.c", "joystick/hidapi/SDL_hidapi_combined.c", "joystick/hidapi/SDL_hidapi_flydigi.c", "joystick/hidapi/SDL_hidapi_gamecube.c", "joystick/hidapi/SDL_hidapi_gip.c", "joystick/hidapi/SDL_hidapi_lg4ff.c", "joystick/hidapi/SDL_hidapi_luna.c", "joystick/hidapi/SDL_hidapi_ps3.c", "joystick/hidapi/SDL_hidapi_ps4.c", "joystick/hidapi/SDL_hidapi_ps5.c", "joystick/hidapi/SDL_hidapi_rumble.c", "joystick/hidapi/SDL_hidapi_shield.c", "joystick/hidapi/SDL_hidapi_sinput.c", "joystick/hidapi/SDL_hidapi_stadia.c", "joystick/hidapi/SDL_hidapi_steam.c", "joystick/hidapi/SDL_hidapi_steamdeck.c", "joystick/hidapi/SDL_hidapi_steam_hori.c", "joystick/hidapi/SDL_hidapi_steam_triton.c", "joystick/hidapi/SDL_hidapi_switch.c", "joystick/hidapi/SDL_hidapi_switch2.c", "joystick/hidapi/SDL_hidapi_wii.c", "joystick/hidapi/SDL_hidapi_xbox360.c", "joystick/hidapi/SDL_hidapi_xbox360w.c", "joystick/hidapi/SDL_hidapi_xboxone.c", "joystick/hidapi/SDL_hidapi_zuiki.c", "joystick/hidapi/SDL_hidapijoystick.c", "joystick/hidapi/SDL_report_descriptor.c"})
        if R.truthy(((R.index(env, "platform") == "linuxbsd"))) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"core/linux/SDL_dbus.c", "core/linux/SDL_evdev.c", "core/linux/SDL_evdev_capabilities.c", "core/linux/SDL_evdev_kbd.c", "core/linux/SDL_threadprio.c", "core/linux/SDL_udev.c", "core/unix/SDL_appid.c", "core/unix/SDL_poll.c", "haptic/linux/SDL_syshaptic.c", "joystick/linux/SDL_sysjoystick.c", "loadso/dlopen/SDL_sysloadso.c", "thread/pthread/SDL_syscond.c", "thread/pthread/SDL_sysmutex.c", "thread/pthread/SDL_sysrwlock.c", "thread/pthread/SDL_syssem.c", "thread/pthread/SDL_systhread.c", "thread/pthread/SDL_systls.c", "timer/unix/SDL_systimer.c"})
            env_sdl:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "core/linux")}})
        else
            if R.truthy(((R.index(env, "platform") == "macos"))) then
                thirdparty_sources = R.iadd(thirdparty_sources, {"core/unix/SDL_appid.c", "core/unix/SDL_poll.c", "haptic/darwin/SDL_syshaptic.c", "joystick/darwin/SDL_iokitjoystick.c", "joystick/apple/SDL_mfijoystick.m", "thread/pthread/SDL_syscond.c", "thread/pthread/SDL_sysmutex.c", "thread/pthread/SDL_sysrwlock.c", "thread/pthread/SDL_syssem.c", "thread/pthread/SDL_systhread.c", "thread/pthread/SDL_systls.c", "timer/unix/SDL_systimer.c"})
            else
                if R.truthy(((R.index(env, "platform") == "windows"))) then
                    thirdparty_sources = R.iadd(thirdparty_sources, {"core/windows/SDL_hid.c", "core/windows/SDL_immdevice.c", "core/windows/SDL_windows.c", "core/windows/SDL_xinput.c", "core/windows/pch.c", "filesystem/SDL_filesystem.c", "filesystem/windows/SDL_sysfilesystem.c", "filesystem/windows/SDL_sysfsops.c", "haptic/windows/SDL_dinputhaptic.c", "haptic/windows/SDL_windowshaptic.c", "joystick/windows/SDL_dinputjoystick.c", "joystick/windows/SDL_rawinputjoystick.c", "joystick/windows/SDL_windows_gaming_input.c", "joystick/windows/SDL_windowsjoystick.c", "joystick/windows/SDL_xinputjoystick.c", "thread/generic/SDL_syscond.c", "thread/generic/SDL_sysrwlock.c", "sensor/windows/SDL_windowssensor.c", "thread/windows/SDL_syscond_cv.c", "thread/windows/SDL_sysmutex.c", "thread/windows/SDL_sysrwlock_srw.c", "thread/windows/SDL_syssem.c", "thread/windows/SDL_systhread.c", "thread/windows/SDL_systls.c", "time/SDL_time.c", "timer/windows/SDL_systimer.c"})
                else
                    if R.truthy((R.contains({"ios", "visionos"}, R.index(env, "platform")))) then
                        thirdparty_sources = R.iadd(thirdparty_sources, {"core/unix/SDL_appid.c", "core/unix/SDL_poll.c", "haptic/dummy/SDL_syshaptic.c", "joystick/apple/SDL_mfijoystick.m", "thread/pthread/SDL_syscond.c", "thread/pthread/SDL_sysmutex.c", "thread/pthread/SDL_sysrwlock.c", "thread/pthread/SDL_syssem.c", "thread/pthread/SDL_systhread.c", "thread/pthread/SDL_systls.c", "timer/unix/SDL_systimer.c"})
                    end
                end
            end
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_thirdparty = env_sdl:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.drivers_sources = R.iadd(env.drivers_sources, thirdparty_obj)
    end
    driver_obj = {}
    env_sdl:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
