fn main() {
    // Simply ensure that changes to WIT files trigger a rebuild
    println!("cargo:rerun-if-changed=src/wit/phira-mp.wit");
    println!("cargo:rerun-if-changed=build.rs");
}