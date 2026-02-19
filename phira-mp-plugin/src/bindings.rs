//! Generated bindings for Phira MP plugin system
//!
//! This module contains the generated bindings from the WIT interface definition.

// Generate bindings using wit-bindgen
wit_bindgen::generate!({
    world: "plugin",
    path: "src/wit/phira-mp.wit",
});