//! `GlobalAlloc` wrapper qui FFI-appelle `heap_caps_malloc` / `heap_caps_free`
//! d'ESP-IDF.
//!
//! Utilise uniquement a la cross-compile `xtensa-esp32s3-none-elf` (target_os =
//! "none"). Sur host (`cargo test`), `std` fournit l'allocateur natif.
//!
//! **Safety contract** : `heap_caps_malloc` et `heap_caps_free` sont exportes
//! en `extern "C"` par le composant `heap` d'ESP-IDF ; ils sont presents dans
//! tout firmware IDF v5+. Le linker `xtensa-esp-elf-ld` resoud ces symboles
//! au moment du link final cote `firmware-idf-v2/`.
//!
//! **Strategie heap** : `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` (0x804) —
//! on n'utilise JAMAIS la PSRAM pour le core Rust :
//! - latence SRAM interne plus deterministe
//! - evite cache coherency issues avec le tick 200 ms
//! - `BmuCore` fait ~1 KiB, 1 seule allocation au boot, zero allocation per-tick

#![cfg(all(not(test), target_os = "none"))]

use core::alloc::{GlobalAlloc, Layout};

// Caps : verifie dans `~/esp/esp-idf/components/heap/include/esp_heap_caps.h`
// pour IDF v5.4 :
//   MALLOC_CAP_8BIT     = (1 << 2)  -> 0x004
//   MALLOC_CAP_INTERNAL = (1 << 11) -> 0x800
// Soit ensemble 0x804.
const CAPS_INTERNAL_8BIT: u32 = 0x804;

extern "C" {
    fn heap_caps_malloc(size: usize, caps: u32) -> *mut u8;
    fn heap_caps_free(ptr: *mut u8);
}

struct EspIdfHeap;

unsafe impl GlobalAlloc for EspIdfHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Layout.align() est ignore : heap_caps_malloc retourne des buffers
        // alignes sur 8 bytes minimum, suffisant pour toutes les structs Rust
        // de bmu-core (alignement max = 8 sur u64 / Option<RintResult>).
        //
        // Si un jour un type avec align > 8 est ajoute, utiliser
        // `heap_caps_aligned_alloc(layout.align(), layout.size(), caps)`.
        debug_assert!(layout.align() <= 8, "alignement > 8 non supporte");
        // SAFETY: FFI call vers ESP-IDF heap component, symbole toujours present.
        unsafe { heap_caps_malloc(layout.size(), CAPS_INTERNAL_8BIT) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        // SAFETY: ptr provient de `alloc` ci-dessus (donc de `heap_caps_malloc`).
        unsafe { heap_caps_free(ptr) };
    }
}

#[global_allocator]
static GLOBAL: EspIdfHeap = EspIdfHeap;
