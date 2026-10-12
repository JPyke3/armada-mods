# Shared steps for the mods' build.sh scripts. Sourced by ../build.sh with
# ROOT (the repository), MOD (the mod id) and STAGE (the package being assembled) set.

DOTNET_IMAGE=mcr.microsoft.com/dotnet/sdk:8.0
BEPINEX_URL='https://builds.bepinex.dev/projects/bepinex_be/788/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788%2B5b766a3.zip'
BEPINEX_SHA256=f4cc496bd098a0df4164b81e3737297707f13a47c2478dba2f60eefab784817a
UE4SS_URL='https://github.com/UE4SS-RE/RE-UE4SS/releases/download/v3.0.1/UE4SS_v3.0.1.zip'
UE4SS_SHA256=4b47d4bceddd2f561a4e395bfa00924ccfc945af576a2d0c613e6537846c57ec

die() {
    echo "build: $MOD: $*" >&2
    exit 1
}

# fetch <url> <sha256> <name>: download once into cache/, verify, print the path
fetch() {
    local file="$ROOT/cache/$3"
    mkdir -p "$ROOT/cache"
    if [[ ! -f "$file" ]]; then
        curl -fsSL --retry 3 -A armada-mods -o "$file.tmp" "$1" || die "download failed: $1"
        mv "$file.tmp" "$file"
    fi
    echo "$2  $file" | sha256sum -c --quiet - >&2 || die "checksum mismatch: $file"
    echo "$file"
}

# unpacked <zip> <name>: unzip once into cache/<name>, print the path
unpacked() {
    local dir="$ROOT/cache/$2"
    if [[ ! -d "$dir" ]]; then
        mkdir -p "$dir.tmp"
        unzip -q -o "$1" -d "$dir.tmp"
        mv "$dir.tmp" "$dir"
    fi
    echo "$dir"
}

# The host's dotnet if there is one, otherwise the SDK container.
run_dotnet() {
    if command -v dotnet >/dev/null; then
        dotnet "$@"
    else
        local refs=()
        # refs/<mod> may be a link to a folder outside the repository
        [[ ! -d "$ROOT/refs/$MOD" ]] || refs=(-v "$(realpath "$ROOT/refs/$MOD"):$ROOT/refs/$MOD")
        mkdir -p "$ROOT/cache/nuget"
        podman run --rm -v "$ROOT:$ROOT" "${refs[@]}" -v "$ROOT/cache/nuget:/root/.nuget" \
            -w "$PWD" "$DOTNET_IMAGE" dotnet "$@"
    fi
}

# compile <project folder> <output folder> [msbuild properties...]
compile() {
    local src="$1" out="$2"
    shift 2
    rm -rf "$out"
    (cd "$src" && run_dotnet build -c Release -o "$out" -v q --nologo "$@" >"$ROOT/build/dotnet.log" 2>&1) ||
        { grep -E ' error ' "$ROOT/build/dotnet.log" | sort -u | head -20 >&2; die "compile failed: $src"; }
}

# Reference assemblies taken from the builder's own copy of the game (see refs.sh).
game_refs() {
    [[ -d "$ROOT/refs/$MOD" ]] || die "no game reference assemblies in refs/$MOD; run ./refs.sh $MOD <game folder>"
    echo "$ROOT/refs/$MOD"
}

bepinex() {
    unpacked "$(fetch "$BEPINEX_URL" "$BEPINEX_SHA256" bepinex6-il2cpp-be788.zip)" bepinex6-il2cpp-be788
}

screen_host() {
    install -D -m 0755 "$ROOT/screen-host/screen-host" "$STAGE/host/screen-host"
}

# The native helper a Unity mod's before_launch hook runs on the device. Built once per run of build.sh.
unity_tools() {
    local out="$ROOT/build/interopgen-linux-arm64"
    if [[ ! -x "$out/interopgen" ]]; then
        rm -rf "$out"
        (cd "$ROOT/lib/unity/interopgen" && run_dotnet publish -c Release -r linux-arm64 --self-contained true \
            -p:InvariantGlobalization=true -p:BepInEx="$(bepinex)" -o "$out" -v q --nologo >"$ROOT/build/dotnet.log" 2>&1) ||
            { grep -E ' error ' "$ROOT/build/dotnet.log" | sort -u | head -20 >&2; die "interopgen publish failed"; }
    fi
    mkdir -p "$STAGE/tools"
    cp -r "$out" "$STAGE/tools/interopgen"
    install -m 0644 "$ROOT/THIRD-PARTY.md" "$STAGE/THIRD-PARTY.md"
}

# unity_mono <game data folder name>: boot assembly into the game's Managed folder,
# the provider from src/ next to the game, and the helper the hook patches with.
unity_mono() {
    local refs out="$ROOT/build/out/$MOD"
    refs="$(game_refs)"
    compile "$ROOT/lib/unity/monoboot" "$out/boot" -p:Refs="$refs"
    compile "$ROOT/$MOD/src" "$out/provider" -p:Refs="$refs"
    install -D -m 0644 "$out/boot/ArmadaSecondScreenBoot.dll" "$STAGE/overlay/$1/Managed/ArmadaSecondScreenBoot.dll"
    mkdir -p "$STAGE/overlay/ArmadaSecondScreen"
    cp "$out"/provider/ArmadaSecondScreen*.dll "$STAGE/overlay/ArmadaSecondScreen/"
    unity_tools
}

# ue4ss <folder of the game's exe, relative to the game folder>
ue4ss() {
    local src dest="$STAGE/overlay/$1"
    src="$(unpacked "$(fetch "$UE4SS_URL" "$UE4SS_SHA256" UE4SS_v3.0.1.zip)" ue4ss-3.0.1)"
    install -m 0644 "$ROOT/THIRD-PARTY.md" "$STAGE/THIRD-PARTY.md"
    install -D -m 0644 "$src/dwmapi.dll" "$dest/dwmapi.dll"
    install -D -m 0644 "$src/UE4SS.dll" "$dest/UE4SS.dll"
    install -D -m 0644 "$src/Mods/Keybinds/Scripts/main.lua" "$dest/Mods/Keybinds/Scripts/main.lua"
    mkdir -p "$dest/Mods/shared"
    cp -r "$src/Mods/shared/jsbProfiler" "$src/Mods/shared/UEHelpers" "$dest/Mods/shared/"
}

# A preload library for a native Linux game: always x86-64, since games on Armada are x86-64
# builds run under emulation. It must load in the oldest C library a game runtime carries.
native_x86_64() {
    local src="$1" out="$2" cc
    mkdir -p "$(dirname "$out")"
    if [[ "$(uname -m)" == x86_64 ]] && command -v gcc >/dev/null; then cc=(gcc)
    elif command -v x86_64-linux-gnu-gcc >/dev/null; then cc=(x86_64-linux-gnu-gcc)
    else
        podman image exists localhost/armada-mods-gcc-x86-64 || podman build -q -t localhost/armada-mods-gcc-x86-64 - >/dev/null <<'IMAGE' || die "could not build the compiler container"
FROM docker.io/library/debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc-x86-64-linux-gnu libc6-dev-amd64-cross && rm -rf /var/lib/apt/lists/*
IMAGE
        cc=(podman run --rm -v "$ROOT:$ROOT" -w "$PWD" localhost/armada-mods-gcc-x86-64 x86_64-linux-gnu-gcc)
    fi
    "${cc[@]}" -O2 -s -Wall -shared -fPIC -fno-stack-protector -o "$out" "$src" -ldl -lpthread || die "compile failed: $src"
    if grep -aoE 'GLIBC_2\.(1[89]|[2-9][0-9])' "$out" | grep -q .; then die "$src needs a newer C library than 2.17"; fi
}

# The compiler for small Windows helpers: the host's MinGW if there is one, otherwise a container.
run_mingw() {
    if command -v x86_64-w64-mingw32-gcc >/dev/null; then
        x86_64-w64-mingw32-gcc "$@"
    else
        podman image exists localhost/armada-mods-mingw || podman build -q -t localhost/armada-mods-mingw - >/dev/null <<'IMAGE' || die "could not build the MinGW container"
FROM docker.io/library/debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc-mingw-w64-x86-64 && rm -rf /var/lib/apt/lists/*
IMAGE
        podman run --rm -v "$ROOT:$ROOT" -w "$PWD" localhost/armada-mods-mingw x86_64-w64-mingw32-gcc "$@"
    fi
}

# UE4SS for games whose start-up protection fails when UE4SS is already scanning the
# process: the stock proxy loads lib/ue4ss-late, which loads the real UE4SS later.
ue4ss_late() {
    local dest="$STAGE/overlay/$1"
    ue4ss "$1/ue4ss"
    mv "$dest/ue4ss/dwmapi.dll" "$dest/dwmapi.dll"
    (cd "$ROOT/lib/ue4ss-late" && run_mingw -O2 -s -Wall -shared -static-libgcc -o "$dest/UE4SS.dll" late.c) ||
        die "compile failed: lib/ue4ss-late"
}
