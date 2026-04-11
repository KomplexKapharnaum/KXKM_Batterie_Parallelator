# Component bmu_core_rs : wrapper ESP-IDF autour du staticlib Rust libbmu_core.a
# produit par le workspace firmware-rust/ via corrosion.
#
# corrosion_import_crate :
#   - lit firmware-rust/Cargo.toml
#   - invoque `cargo build --target xtensa-esp32s3-none-elf --release -p bmu-core`
#     (via `cargo +esp` — toolchain selectionne par rust-toolchain.toml du workspace)
#   - expose la cible CMake `bmu_core-static` (underscore) qui est linkee au binaire
#
# Le header `bmu_core.h` est vendore dans include/ via `cargo xtask vendor-header`
# (Phase 12 Task 12.2). Ne PAS le regenerer cote CMake : on veut un artefact
# explicite en git pour audit et review. Phase 22 ajoutera un check CI.

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_common heap
)

include(FetchContent)

# IMPORTANT : ces variables doivent etre fixees AVANT find_package(Corrosion).
# On pointe directement sur les binaires de la toolchain `esp` pour bypasser
# totalement le rustup proxy (qui confond `--toolchain` dans certaines
# versions de cargo stable) et eviter que la detection de libs host via
# rust-toolchain.toml du workspace bmu-core ne pollue la cible xtensa.
set(Rust_CARGO_TARGET "xtensa-esp32s3-none-elf" CACHE STRING "Cargo target triple" FORCE)
set(Rust_COMPILER "$ENV{HOME}/.rustup/toolchains/esp/bin/rustc" CACHE FILEPATH "rustc esp toolchain" FORCE)
set(Rust_CARGO "$ENV{HOME}/.rustup/toolchains/esp/bin/cargo" CACHE FILEPATH "cargo esp toolchain" FORCE)
set(Rust_RESOLVE_RUSTUP_TOOLCHAINS OFF CACHE BOOL "Bypass rustup proxy" FORCE)

# Pin corrosion v0.6.0 — aligne spec Part 2 Section 11.3
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.0
)
FetchContent_MakeAvailable(Corrosion)

# Import du staticlib Rust. MANIFEST_PATH est relatif a la racine du projet
# ESP-IDF (${CMAKE_SOURCE_DIR} = firmware-idf-v2/).
corrosion_import_crate(
    MANIFEST_PATH "${CMAKE_SOURCE_DIR}/../firmware-rust/Cargo.toml"
    CRATES bmu-core
    PROFILE release
    LOCKED
    FLAGS -Zbuild-std=core,alloc
)

# Linker le staticlib au component. Corrosion v0.6.0 normalise le nom du
# crate `bmu-core` (avec tiret) en cible CMake `bmu_core-static` (avec
# underscore + suffixe `-static` pour le STATIC_LIBRARY IMPORTED). C'est
# cette cible-la qu'on link dans le COMPONENT_LIB ESP-IDF.
target_link_libraries(${COMPONENT_LIB} INTERFACE bmu_core-static)
