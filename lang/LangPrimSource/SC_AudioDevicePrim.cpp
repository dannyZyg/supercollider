/*
    SuperCollider real time audio synthesis system
    Copyright (c) 2002 James McCartney. All rights reserved.
    http://www.audiosynth.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#if defined(SC_AUDIO_API_COREAUDIO)
#    include <CoreAudio/AudioHardware.h>
#elif defined(SC_AUDIO_API_PORTAUDIO)
#    include "portaudio.h"
#    include "SC_PaUtils.hpp"
#endif

#include "SCBase.h"
#include "VMGlobals.h"
#include "PyrKernel.h"
#include "PyrPrimitive.h"
#include "GC.h"

enum { OUT = 0, IN, BOTH };

#if defined(SC_AUDIO_API_COREAUDIO)
int listDevices(VMGlobals* g, int type) {
    int numDevices, num = 0;
    PyrSlot* a = g->sp - 2;
    AudioObjectPropertyAddress propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDevices;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMaster;

    //	std::uint64_t count;
    UInt32 count;
    //    OSStatus err = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &count, 0);
    OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &count);
    AudioDeviceID* devices = (AudioDeviceID*)malloc(count);
    // err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &count, devices);
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &count, devices);
    if (err != kAudioHardwareNoError) {
        free(devices);
        return 0;
    }

    numDevices = count / sizeof(AudioDeviceID);

    int i;

    if (type < BOTH) {
        if (type < IN) {
            propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
        } else {
            propertyAddress.mScope = kAudioDevicePropertyScopeInput;
        }

        for (i = 0; i < numDevices; i++) {
            Boolean writeable;
            propertyAddress.mSelector = kAudioDevicePropertyStreams;
            // err = AudioDeviceGetPropertyInfo(devices[i], 0, type, kAudioDevicePropertyStreams, &count, &writeable);

            err = AudioObjectGetPropertyDataSize(devices[i], &propertyAddress, 0, NULL, &count);

            if (err != kAudioHardwareNoError) {
                free(devices);
                return 0;
            }

            err = AudioObjectIsPropertySettable(devices[i], &propertyAddress, &writeable);

            if (err != kAudioHardwareNoError) {
                free(devices);
                return 0;
            }
            if (!count)
                devices[i] = 0;
            else
                num++;
        }
    } else
        num = numDevices;

    PyrObject* devArray = newPyrArray(g->gc, num * sizeof(PyrObject), 0, true);
    SetObject(a, devArray); // this is okay here as we don't use the receiver below

    int j = 0;
    for (i = 0; i < numDevices; i++) {
        if (!devices[i])
            continue;

        propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
        propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;

        err = AudioObjectGetPropertyDataSize(devices[i], &propertyAddress, 0, NULL, &count);
        if (err != kAudioHardwareNoError) {
            break;
        }

        CFStringRef nameRef;
        err = AudioObjectGetPropertyData(devices[i], &propertyAddress, 0, NULL, &count, &nameRef);

        if (err == kAudioHardwareNoError) {
            const char* c_str = CFStringGetCStringPtr(nameRef, kCFStringEncodingUTF8);

            PyrString* string;

            if (c_str) {
                string = newPyrString(g->gc, c_str, 0, true);
            } else {
                // Fallback for when the direct pointer isn't available
                CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(nameRef), kCFStringEncodingUTF8) + 1;
                char* nameBytes = new char[size];
                if (CFStringGetCString(nameRef, nameBytes, size, kCFStringEncodingUTF8)) {
                    string = newPyrString(g->gc, nameBytes, 0, true);
                } else {
                    delete[] nameBytes;
                    CFRelease(nameRef);
                    break;
                }
                delete[] nameBytes;
            }
            SetObject(devArray->slots + j, string);
            devArray->size++;
            g->gc->GCWriteNew(devArray, (PyrObject*)string);

            CFRelease(nameRef);
            j++;
        }
    }

    free(devices);
    return 1;
}

#elif defined(SC_AUDIO_API_PORTAUDIO)
int listDevices(VMGlobals* g, int type) {
    if (Pa_Initialize() != paNoError)
        return 0;

    auto numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        Pa_Terminate();
        return 0;
    }

    std::vector<const PaDeviceInfo*> deviceInfos;
    for (PaDeviceIndex i = 0; i < numDevices; i++) {
        auto* pdi = Pa_GetDeviceInfo(i);
        if (type == IN) {
            if (pdi->maxInputChannels > 0)
                deviceInfos.push_back(pdi);
        } else if (type == OUT) {
            if (pdi->maxOutputChannels > 0)
                deviceInfos.push_back(pdi);
        } else
            deviceInfos.push_back(pdi);
    }

    PyrObject* devArray = newPyrArray(g->gc, deviceInfos.size() * sizeof(PyrObject), 0, true);
    SetObject(g->sp - 2, devArray); // this is okay here as we don't use the receiver below
    devArray->size = deviceInfos.size();

    for (int i = 0; i < deviceInfos.size(); i++) {
        PyrString* string = newPyrString(g->gc, GetPaDeviceName(deviceInfos[i]).c_str(), 0, true);
        SetObject(devArray->slots + i, string);
        g->gc->GCWriteNew(devArray,
                          reinterpret_cast<PyrObject*>(string)); // we know array is white so we can use GCWriteNew
    }

    Pa_Terminate();
    return 1;
}

#else
int listDevices(VMGlobals* g, int type) {
    return 0; // listing devices fails when using neither CoreAudio nor PortAudio
}
#endif


int prListAudioDevices(VMGlobals* g, int numArgsPushed) {
    int in = 0;
    int out = 0;
    slotIntVal(g->sp, &out);
    slotIntVal(g->sp - 1, &in);

    int type;
    if (in && out)
        type = BOTH;
    else if (in)
        type = IN;
    else
        type = OUT;

    if (listDevices(g, type))
        return errNone;
    return errFailed;
}

void initAudioDevicePrimitives() {
    definePrimitive(nextPrimitiveIndex(), 0, "_ListAudioDevices", prListAudioDevices, 3, 0);
}
