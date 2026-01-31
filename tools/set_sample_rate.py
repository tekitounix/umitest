#!/usr/bin/env python3
"""macOS CoreAudio sample rate setter using ctypes"""

import ctypes
import sys
import time

# Load CoreAudio and CoreFoundation frameworks
coreaudio = ctypes.CDLL('/System/Library/Frameworks/CoreAudio.framework/CoreAudio')
cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')

# Define function signatures for CoreAudio
coreaudio.AudioObjectGetPropertyDataSize.argtypes = [
    ctypes.c_uint32,  # inObjectID
    ctypes.c_void_p,  # inAddress
    ctypes.c_uint32,  # inQualifierDataSize
    ctypes.c_void_p,  # inQualifierData
    ctypes.POINTER(ctypes.c_uint32)  # outDataSize
]
coreaudio.AudioObjectGetPropertyDataSize.restype = ctypes.c_int32

coreaudio.AudioObjectGetPropertyData.argtypes = [
    ctypes.c_uint32,  # inObjectID
    ctypes.c_void_p,  # inAddress
    ctypes.c_uint32,  # inQualifierDataSize
    ctypes.c_void_p,  # inQualifierData
    ctypes.POINTER(ctypes.c_uint32),  # ioDataSize
    ctypes.c_void_p   # outData
]
coreaudio.AudioObjectGetPropertyData.restype = ctypes.c_int32

coreaudio.AudioObjectSetPropertyData.argtypes = [
    ctypes.c_uint32,  # inObjectID
    ctypes.c_void_p,  # inAddress
    ctypes.c_uint32,  # inQualifierDataSize
    ctypes.c_void_p,  # inQualifierData
    ctypes.c_uint32,  # inDataSize
    ctypes.c_void_p   # inData
]
coreaudio.AudioObjectSetPropertyData.restype = ctypes.c_int32

# CoreAudio constants
kAudioHardwarePropertyDevices = 0x64657623  # 'dev#'
kAudioObjectSystemObject = 1
kAudioDevicePropertyNominalSampleRate = 0x6e737274  # 'nsrt'
kAudioObjectPropertyScopeGlobal = 0x676c6f62  # 'glob'
kAudioObjectPropertyScopeOutput = 0x6f757470  # 'outp'
kAudioObjectPropertyScopeInput = 0x696e7074  # 'inpt'
kAudioObjectPropertyElementMain = 0
kAudioObjectPropertyName = 0x6c6e616d  # 'lnam'
kAudioDevicePropertyAvailableNominalSampleRates = 0x6e737223  # 'nsr#'

class AudioObjectPropertyAddress(ctypes.Structure):
    _fields_ = [
        ('mSelector', ctypes.c_uint32),
        ('mScope', ctypes.c_uint32),
        ('mElement', ctypes.c_uint32),
    ]

def get_device_name(dev_id):
    """Get device name as string"""
    addr = AudioObjectPropertyAddress(
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    )
    name_ref = ctypes.c_void_p()
    size = ctypes.c_uint32(8)
    
    result = coreaudio.AudioObjectGetPropertyData(
        dev_id, ctypes.byref(addr), 0, None,
        ctypes.byref(size), ctypes.byref(name_ref)
    )
    
    if result != 0:
        return None
    
    cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
    buf = ctypes.create_string_buffer(256)
    cf.CFStringGetCString(name_ref, buf, 256, 0x08000100)  # UTF-8
    cf.CFRelease(name_ref)
    return buf.value.decode('utf-8')

def get_all_devices():
    """Get list of all audio device IDs"""
    addr = AudioObjectPropertyAddress(
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    )
    size = ctypes.c_uint32()
    
    coreaudio.AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, ctypes.byref(addr),
        0, None, ctypes.byref(size)
    )
    
    num_devices = size.value // 4
    devices = (ctypes.c_uint32 * num_devices)()
    
    coreaudio.AudioObjectGetPropertyData(
        kAudioObjectSystemObject, ctypes.byref(addr),
        0, None, ctypes.byref(size), devices
    )
    
    return list(devices)

def get_sample_rate(dev_id):
    """Get current sample rate of device"""
    addr = AudioObjectPropertyAddress(
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    )
    rate = ctypes.c_double()
    size = ctypes.c_uint32(8)
    
    result = coreaudio.AudioObjectGetPropertyData(
        dev_id, ctypes.byref(addr), 0, None,
        ctypes.byref(size), ctypes.byref(rate)
    )
    
    if result != 0:
        return None
    return rate.value

def set_sample_rate(dev_id, rate, retries=5, delay=0.2):
    """Set sample rate of device with verification retries"""
    # Try Output scope first (for playback devices)
    for scope_name, scope in [("Output", kAudioObjectPropertyScopeOutput), 
                               ("Global", kAudioObjectPropertyScopeGlobal)]:
        addr = AudioObjectPropertyAddress(
            kAudioDevicePropertyNominalSampleRate,
            scope,
            kAudioObjectPropertyElementMain
        )
        new_rate = ctypes.c_double(float(rate))
        
        print(f"Trying scope {scope_name}...")
        
        result = coreaudio.AudioObjectSetPropertyData(
            ctypes.c_uint32(dev_id), 
            ctypes.byref(addr), 
            ctypes.c_uint32(0), 
            None,
            ctypes.c_uint32(8), 
            ctypes.byref(new_rate)
        )
        
        print(f"  AudioObjectSetPropertyData returned: {result}")
        
        if result == 0:
            # Check if it actually changed (CoreAudio can lag)
            for attempt in range(1, retries + 1):
                actual = get_sample_rate(dev_id)
                if actual and abs(actual - rate) < 100:
                    print(f"  Success with {scope_name} scope! (attempt {attempt})")
                    return True
                time.sleep(delay)
            print(f"  API returned 0 but rate is still {actual}")
        else:
            errors = {
                -50: "paramErr",
                560947818: "kAudioHardwareUnknownPropertyError", 
                561211770: "kAudioHardwareBadObjectError",
                2003329396: "kAudioHardwareUnsupportedOperationError",
                560227702: "kAudioHardwareNotRunningError",
            }
            err_name = errors.get(result, f"unknown")
            print(f"  Error: {result} ({err_name})")
    
    return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 set_sample_rate.py <rate>")
        print("       python3 set_sample_rate.py list")
        print("Example: python3 set_sample_rate.py 96000")
        return 1
    
    devices = get_all_devices()
    
    if sys.argv[1] == "list":
        print("Audio devices:")
        for dev_id in devices:
            name = get_device_name(dev_id)
            rate = get_sample_rate(dev_id)
            if name and rate:
                print(f"  [{dev_id}] {name}: {rate:.0f} Hz")
        return 0
    
    target_rate = int(sys.argv[1])
    
    # Find UMI device
    umi_device = None
    for dev_id in devices:
        name = get_device_name(dev_id)
        if name and 'UMI' in name:
            umi_device = dev_id
            print(f"Found: {name} (ID: {dev_id})")
            break
    
    if umi_device is None:
        print("Error: UMI device not found")
        return 1
    
    # Get current rate
    current_rate = get_sample_rate(umi_device)
    print(f"Current rate: {current_rate:.0f} Hz")
    
    # Get available sample rates
    print("Querying available sample rates...")
    addr = AudioObjectPropertyAddress(
        kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    )
    size = ctypes.c_uint32()
    result = coreaudio.AudioObjectGetPropertyDataSize(
        ctypes.c_uint32(umi_device), ctypes.byref(addr), 0, None, ctypes.byref(size)
    )
    if result == 0 and size.value > 0:
        # AudioValueRange is {Float64 mMinimum, Float64 mMaximum}
        num_ranges = size.value // 16
        class AudioValueRange(ctypes.Structure):
            _fields_ = [('mMinimum', ctypes.c_double), ('mMaximum', ctypes.c_double)]
        ranges = (AudioValueRange * num_ranges)()
        result = coreaudio.AudioObjectGetPropertyData(
            ctypes.c_uint32(umi_device), ctypes.byref(addr), 0, None,
            ctypes.byref(size), ctypes.byref(ranges)
        )
        if result == 0:
            print(f"Available sample rates ({num_ranges} ranges):")
            for r in ranges:
                if r.mMinimum == r.mMaximum:
                    print(f"  {r.mMinimum:.0f} Hz")
                else:
                    print(f"  {r.mMinimum:.0f} - {r.mMaximum:.0f} Hz")
    else:
        print(f"Could not get available sample rates (result={result}, size={size.value})")
    
    # Set new rate
    print(f"Setting rate to: {target_rate} Hz")
    success = set_sample_rate(umi_device, target_rate)
    
    if success:
        new_rate = get_sample_rate(umi_device)
        print(f"New rate: {new_rate:.0f} Hz")
        print("Success!")
    else:
        print("Failed to set sample rate")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
