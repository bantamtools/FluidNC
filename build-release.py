#!/usr/bin/env python

# Build FluidNC release bundles (.zip files) for each host platform

from shutil import copy
import subprocess, os, sys, shutil
import io, hashlib

verbose = '-v' in sys.argv

environ = dict(os.environ)

def buildEmbeddedPage():
    print('Building embedded web page')
    return subprocess.run(["python", "build.py"], cwd="embedded").returncode

def buildEnv(pioEnv, verbose=True, extraArgs=None):
    cmd = ['platformio','run', '--disable-auto-clean', '-e', pioEnv]
    if extraArgs:
        cmd.append(extraArgs)
    displayName = pioEnv
    print('Building firmware for ' + displayName)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode

def buildFs(pioEnv, verbose=verbose, extraArgs=None):
    cmd = ['platformio','run', '--disable-auto-clean', '-e', pioEnv, '-t', 'buildfs']
    if extraArgs:
        cmd.append(extraArgs)
    print('Building file system for ' + pioEnv)
    if verbose:
        app = subprocess.Popen(cmd, env=environ)
    else:
        app = subprocess.Popen(cmd, env=environ, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in app.stdout:
            line = line.decode('utf8')
            if "Took" in line or 'Uploading' in line or ("error" in line.lower() and "Compiling" not in line):
                print(line, end='')
    app.wait()
    print()
    return app.returncode

tag = (
    subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
    .strip()
    .decode("utf-8")
)
version = tag.lstrip('v')




relPath = os.path.join('release')
if not os.path.exists(relPath):
    os.makedirs(relPath)

manifestRelPath = os.path.join(relPath, 'current')
if os.path.exists(manifestRelPath):
    shutil.rmtree(manifestRelPath)

os.makedirs(manifestRelPath)

manifest = {
        "name": "FluidNC",
        "version": version,
        "source_url": "https://github.com/bdring/FluidNC/tree/" + tag,
        "release_url": "https://github.com/bdring/FluidNC/releases/tag/" + tag,
        "funding_url": "https://www.paypal.com/donate/?hosted_button_id=8DYLB6ZYYDG7Y",
        "images": {},
        "installable": {
            "name": "installable",
            "description": "Things you can install",
            "choice-name": "Processor type",
            "choices": []
        },
}

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scripts'))
from release_manifest import configs_manifest_fields  # noqa: E402
try:
    manifest.update(configs_manifest_fields(os.path.join('FluidNC', 'data')))
except FileNotFoundError as e:
    print(e)
    sys.exit(1)

# We avoid doing this every time, instead checking in a new NoFile.h as necessary
# if buildEmbeddedPage() != 0:
#    sys.exit(1)

# if buildFs('wifi', verbose=verbose) != 0:
#     sys.exit(1)

def addImage(name, offset, filename, srcpath, dstpath):
    fulldstpath = os.path.join(manifestRelPath,os.path.normpath(dstpath))

    os.makedirs(fulldstpath, exist_ok=True)

    fulldstfile = os.path.join(fulldstpath, filename)

    shutil.copy(os.path.join(srcpath, filename), fulldstfile)

    print("image ", name)

    with open(fulldstfile, "rb") as f:
        data = f.read()
    image = {
        # "name": name,
        "size": os.path.getsize(fulldstfile),
        "offset": offset,
        "path": dstpath + '/' + filename,
        "signature": {
            "algorithm": "SHA2-256",
            "value": hashlib.sha256(data).hexdigest()
        }
    }
    if manifest['images'].get(name) != None:
        print("Duplicate image name", name)
        sys.exit(1)
    manifest['images'][name] = image
    # manifest['images'].append(image)

flashsize = "4m"

mcu = "esp32"
for mcu in ['esp32']:
    for envName in ['wifi_s3', 'wifi_s3_usb-otg']:
        if buildEnv(envName, verbose=verbose) != 0:
            sys.exit(1)
        buildDir = os.path.join('.pio', 'build', envName)
        shutil.copy(os.path.join(buildDir, 'firmware.elf'), os.path.join(relPath, envName + '-' + 'firmware.elf'))
        addImage(mcu + '-' + envName + '-firmware', '0x10000', 'firmware.bin', buildDir, mcu + '/' + envName)
        if envName.startswith("wifi"):
            if buildFs(envName, verbose=verbose) != 0:
                sys.exit(1)
            # bootapp is a data partition that the bootloader and OTA use to determine which
            # image to run.  Its initial value is in a file "boot_app0.bin" in the platformio
            # framework package.  We copy it to the build directory so addImage can find it
            bootappsrc = os.path.join(os.path.expanduser('~'),'.platformio','packages','framework-arduinoespressif32','tools','partitions', 'boot_app0.bin')
            shutil.copy(bootappsrc, buildDir)
            addImage(mcu + '-' + envName + '-' + flashsize + '-filesystem', '0x3d0000', 'littlefs.bin', buildDir, mcu + '/' + envName + '/' + flashsize)
            addImage(mcu + '-' + envName + '-' + flashsize + '_s3-partitions', '0x8000', 'partitions.bin', buildDir, mcu + '/' + flashsize)
            addImage(mcu + '-' + envName +'_s3-bootloader', '0x1000', 'bootloader.bin', buildDir, mcu)
            addImage(mcu + '-' + envName +'_s3-bootapp', '0xe000', 'boot_app0.bin', buildDir, mcu)


def addSection(node, name, description, choice):
    section = {
        "name": name,
        "description": description,
    }
    if choice != None:
        section['choice-name'] = choice
        section['choices'] = []
    node.append(section)

def addMCU(name, description, choice=None):
    addSection(manifest['installable']['choices'], name, description, choice)

def addVariant(variant, description, choice=None):
    node1 = manifest['installable']['choices']
    node1len = len(node1)
    addSection(node1[node1len-1]['choices'], variant, description, choice)

def addInstallable(install_type, erase, images):
    for image in images:
        if manifest['images'].get(image) == None:
            # imagefiles = [obj for obj in manifest['images'] if obj['name'] == image]
            # if len(imagefiles) == 0:
            print("Missing image", image)
            sys.exit(1)
        # if len(imagefiles) > 1:
        #    print("Duplicate image", image)
        #    sys.exit(2)
                      
    node1 = manifest['installable']['choices']
    node1len = len(node1)
    node2 = node1[node1len-1]['choices']
    node2len = len(node2)
    installable = {
        "name": install_type["name"],
        "description": install_type["description"],
        "erase": erase,
        "images": images
    }
    node2[node2len-1]['choices'].append(installable)

fresh_install = { "name": "fresh-install", "description": "Complete FluidNC installation, erasing all previous data"}
firmware_update = { "name": "firmware-update", "description": "Update FluidNC to latest firmware version, preserving previous filesystem data."}
filesystem_update = { "name": "filesystem-update", "description": "Update FluidNC filesystem only, erasing previous filesystem data."}

def makeManifest():
    addMCU("esp32", "ESP32-WROOM", "Firmware variant")

    addVariant("wifi_s3", "Supports WiFi and WebUI on the esp32_s3", "Installation type")
    addInstallable(fresh_install, True, ["esp32-wifi_s3-4m_s3-partitions", "esp32-wifi_s3_s3-bootloader", "esp32-wifi_s3_s3-bootapp", "esp32-wifi_s3-firmware", "esp32-wifi_s3-4m-filesystem"])
    addInstallable(firmware_update, False, ["esp32-wifi_s3-firmware"])
    addInstallable(filesystem_update, False, ["esp32-wifi_s3-4m-filesystem"])

    addVariant("wifi_s3_usb-otg", "Supports WiFi and WebUI on the esp32_s3 with runtime USB configuration", "Installation type")
    addInstallable(fresh_install, True, ["esp32-wifi_s3_usb-otg-4m_s3-partitions", "esp32-wifi_s3_usb-otg_s3-bootloader", "esp32-wifi_s3_usb-otg_s3-bootapp", "esp32-wifi_s3_usb-otg-firmware", "esp32-wifi_s3_usb-otg-4m-filesystem"])
    addInstallable(firmware_update, False, ["esp32-wifi_s3_usb-otg-firmware"])
    addInstallable(filesystem_update, False, ["esp32-wifi_s3_usb-otg-4m-filesystem"])

makeManifest()

import json
def printManifest():
    print(json.dumps(manifest, indent=2))

with open(os.path.join(manifestRelPath, "manifest.json"), "w") as manifest_file:
    json.dump(manifest, manifest_file, indent=2)
                 


from release_assets import write_images_zip, write_elf_zip  # noqa: E402  (scripts/ is on sys.path above)
imagesZip = os.path.join(relPath, f'fluidnc-bantam-{version}-images.zip')
print("images zip:", imagesZip, write_images_zip(manifestRelPath, imagesZip))
elfZip = os.path.join(relPath, f'fluidnc-bantam-{version}-elf.zip')
elfs = [os.path.join(relPath, envName + '-firmware.elf') for envName in ['wifi_s3', 'wifi_s3_usb-otg']]
print("elf zip:", elfZip, write_elf_zip(elfs, elfZip))
for e in elfs:
    os.remove(e)   # the loose ELFs are no longer release assets

sys.exit(0)
