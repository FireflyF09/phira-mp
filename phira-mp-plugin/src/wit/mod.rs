//! WIT interface definitions for Phira MP plugin system

// Export the WIT file path for use in bindings generation
pub const WIT_PATH: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/src/wit/phira-mp.wit");