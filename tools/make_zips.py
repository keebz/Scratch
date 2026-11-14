#!/usr/bin/env python3
"""Build the numbered SD-card zips:  python3 plugins/Scratch/tools/make_zips.py <build number>
Run from anywhere. Writes dist/Scratch-b<NN>-SD-card.zip and dist/Scratch-b<NN>-DEBUG-SD-card.zip
(the debug one writes scratch_log.txt whose first line names the build). Each zip also holds BUILD.txt."""
import os, shutil, subprocess, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__)); PLUG = os.path.dirname(HERE); SDK = os.path.dirname(os.path.dirname(PLUG))
n = int(sys.argv[1]); tag = "b%02d" % n
def skin():
    subprocess.run([os.path.join(os.environ.get('SCRATCH_VENV', ''), 'bin', 'python') if os.environ.get('SCRATCH_VENV') else sys.executable, os.path.join(HERE, 'make_skin.py')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'bake_text.py')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'bake_help.py')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'make_quickstart.py')], check=True, stdout=subprocess.DEVNULL)


def build(debug):
    cmd = [sys.executable, os.path.join(SDK, "tools", "build.py"), "plugins/Scratch", "--define", "SCRATCH_BUILD=%d" % n]
    if debug: cmd += ["--define", "SCRATCH_DEBUG=1"]
    subprocess.run(cmd, cwd=SDK, check=True, stdout=subprocess.DEVNULL)
def pack(name, debug):
    sd = os.path.join(PLUG, "dist", "sd"); shutil.rmtree(sd, ignore_errors=True)
    dst = os.path.join(sd, "VST2 Plugins"); os.makedirs(dst)
    shutil.copytree(os.path.join(PLUG, "build", "Keebz - VST - Scratch"), os.path.join(dst, "Keebz - VST - Scratch"))
    with open(os.path.join(dst, "Keebz - VST - Scratch", "BUILD.txt"), "w") as f:
        f.write("Keebz Scratch, build %d%s\n" % (n, " (debug: writes scratch_log.txt)" if debug else ""))
    shutil.copy(os.path.join(PLUG, "docs", "Scratch-QuickStart.pdf"), os.path.join(sd, "Scratch Quick Start.pdf"))   # next to VST2 Plugins, not inside it
    out = os.path.join(PLUG, "dist", name)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for r, _, fs in os.walk(sd):
            for fn in fs:
                p = os.path.join(r, fn); z.write(p, os.path.relpath(p, sd))
    shutil.rmtree(sd); print(out)
skin()
build(False); pack("Scratch-%s-SD-card.zip" % tag, False)
build(True);  pack("Scratch-%s-DEBUG-SD-card.zip" % tag, True)
build(False)
