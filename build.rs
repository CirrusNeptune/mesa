use std::env;
use std::path::PathBuf;

fn main() {
    let build_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let build_path = build_path.to_str().unwrap();
    panic!("{}", env::current_dir().unwrap().to_str().unwrap());
    meson::build(".", build_path);

    println!(
        "cargo:rustc-env=VC4_COMPILER_BIN={}/src/vc4-glsl/vc4-glsl",
        build_path
    );
}
