#!/usr/bin/env python3
"""Gera Android/MDS.jucer a partir dos arquivos do projeto. Depois: Projucer --resave Android/MDS.jucer"""
import glob, os, hashlib
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); os.chdir(root)
i = lambda x: hashlib.md5(x.encode()).hexdigest()[:6]
mods = "juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils juce_core juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra".split()
src = ["Source/Mobile/Main.cpp", "Source/Engine/Model.cpp", "Source/Engine/Engine.cpp", "Source/Engine/Package.cpp"]
hdr = ["Source/Engine/Model.h", "Source/Engine/Engine.h", "Source/Engine/Sounds.h", "Source/Engine/SoundIndex.h", "Source/Engine/Package.h", "Source/UI/Common.h"]
files = ''.join(f'      <FILE id="{i(f)}" name="{os.path.basename(f)}" compile="1" resource="0" file="../{f}"/>\n' for f in src)
files += ''.join(f'      <FILE id="{i(f)}" name="{os.path.basename(f)}" compile="0" resource="0" file="../{f}"/>\n' for f in hdr)
sounds = ''.join(f'      <FILE id="{i(f)}" name="{os.path.basename(f)}" compile="0" resource="1" file="../{f}"/>\n' for f in sorted(glob.glob('Assets/sounds/*')))
defs = "JUCE_CORE_INCLUDE_JNI_HELPERS=1 LINK_PLATFORM_LINUX=1 LINK_PLATFORM_UNIX=1 ASIO_STANDALONE=1 ASIO_NO_TYPEID=1 ASIO_VERSION_NAMESPACE=link_asio_1_38_2"
hp = "../../../Source;../../../third_party/link/include;../../../third_party/link/modules/asio-standalone/asio/include"
nl = chr(10)
x = f'''<?xml version="1.0" encoding="UTF-8"?>

<JUCERPROJECT id="BtMb01" name="MDS" projectType="guiapp" jucerFormatVersion="1" version="0.2.0"
              bundleIdentifier="com.mds.mobile" companyName="MDS" displaySplashScreen="0"
              reportAppUsage="0" cppLanguageStandard="17" defines="{defs}">
  <MAINGROUP id="BtMb02" name="MDS">
    <GROUP id="BtMb03" name="Source">
{files}    </GROUP>
    <GROUP id="BtMb05" name="Icon">
      <FILE id="BtIc01" name="icon.png" compile="0" resource="1" file="../Assets/icon.png"/>
    </GROUP>
    <GROUP id="BtMb04" name="Sounds">
{sounds}    </GROUP>
  </MAINGROUP>
  <MODULES>
{''.join(f'    <MODULE id="{m}" showAllCode="1" useLocalCopy="0" useGlobalPath="0"/>{nl}' for m in mods)}  </MODULES>
  <JUCEOPTIONS JUCE_STRICT_REFCOUNTEDPOINTER="1" JUCE_WEB_BROWSER="0" JUCE_USE_CURL="0" JUCE_USE_OGGVORBIS="1" JUCE_USE_FLAC="1"/>
  <EXPORTFORMATS>
    <ANDROIDSTUDIO targetFolder="Builds/Android" androidMinimumSDK="24" androidTargetSDK="34"
                   androidScreenOrientation="portrait" androidInternetNeeded="1" androidBluetoothNeeded="1"
                   androidBluetoothScanNeeded="1" androidBluetoothConnectNeeded="1"
                   androidOtherPermissions="android.permission.CHANGE_WIFI_MULTICAST_STATE android.permission.ACCESS_WIFI_STATE android.permission.ACCESS_NETWORK_STATE android.permission.WAKE_LOCK"
                   androidKeyStore="../../../batuta.keystore" androidKeyStorePass="batuta" androidKeyAlias="batuta" androidKeyAliasPass="batuta"
                   androidVersionCode="3" smallIcon="BtIc01" bigIcon="BtIc01">
      <CONFIGURATIONS>
        <CONFIGURATION name="Debug" isDebug="1" optimisation="1" targetName="MDS" headerPath="{hp}" androidArchitectures="arm64-v8a armeabi-v7a x86_64"/>
        <CONFIGURATION name="Release" isDebug="0" optimisation="3" targetName="MDS" headerPath="{hp}" androidArchitectures="arm64-v8a armeabi-v7a x86_64"/>
      </CONFIGURATIONS>
      <MODULEPATHS>
{''.join(f'        <MODULEPATH id="{m}" path="../third_party/JUCE/modules"/>{nl}' for m in mods)}      </MODULEPATHS>
    </ANDROIDSTUDIO>
  </EXPORTFORMATS>
</JUCERPROJECT>
'''
open('Android/MDS.jucer', 'w').write(x)
