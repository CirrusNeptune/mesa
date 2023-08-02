use serde_json::Value;
use std::env;
use std::path::PathBuf;
use std::process::Command;

/// Runs meson and/or ninja to build a project.
pub fn build(project_dir: &str, build_dir: &str) {
    run_meson(project_dir, build_dir);
}

fn run_meson(lib: &str, dir: &str) {
    if !is_configured(dir) {
        run_command(lib, "meson", &["setup", ".", dir]);
    } else if !does_source_dir_match(lib, dir) {
        run_command(lib, "meson", &["setup", "--reconfigure", ".", dir]);
        assert!(does_source_dir_match(lib, dir));
    }
    run_command(dir, "ninja", &[]);
}

fn run_command(dir: &str, name: &str, args: &[&str]) {
    let mut cmd = Command::new(name);
    cmd.current_dir(dir);
    if args.len() > 0 {
        cmd.args(args);
    }
    let status = cmd.status().expect("cannot run command");
    assert!(status.success());
}

fn is_configured(dir: &str) -> bool {
    let mut path = PathBuf::from(dir);
    path.push("build.ninja");
    return path.as_path().exists();
}

fn does_source_dir_match(lib: &str, dir: &str) -> bool {
    let path = PathBuf::from(dir)
        .join("meson-info")
        .join("meson-info.json");
    let meson_json_f = std::fs::File::open(path.as_path()).unwrap();
    let meson_json: Value = serde_json::from_reader(meson_json_f).unwrap();
    let source_dir = meson_json["directories"]["source"].as_str().unwrap();
    return lib == source_dir;
}

fn main() {
    let build_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let build_path = build_path.to_str().unwrap();
    build(env::current_dir().unwrap().to_str().unwrap(), build_path);

    println!(
        "cargo:rustc-env=VC4_COMPILER_BIN={}/src/vc4-glsl/vc4-glsl",
        build_path
    );
}
