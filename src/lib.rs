use std::env;
use std::ffi::OsStr;
use std::fmt::Debug;

pub fn compile_shader<S: AsRef<OsStr> + Debug>(
    vert_path: S,
    frag_path: S,
    rs_out_path: S,
) -> Result<(), String> {
    use std::process::Command;

    let vc4_compiler_bin =
        env::var("VC4_COMPILER_BIN").unwrap_or_else(|_| env!("VC4_COMPILER_BIN").to_string());

    let output = Command::new(&vc4_compiler_bin)
        .arg(&vert_path)
        .arg(&frag_path)
        .arg(&rs_out_path)
        .output();

    match output {
        Ok(ref out) => {
            if !out.status.success() {
                if let Ok(stderr) = String::from_utf8(output.unwrap().stderr) {
                    return Err(format!(
                        "{} {:?} {:?} {:?}\n{}",
                        vc4_compiler_bin, vert_path, frag_path, rs_out_path, stderr
                    ));
                }
            }
        }
        Err(e) => {
            return Err(format!(
                "{} {:?} {:?} {:?}\n{}",
                vc4_compiler_bin,
                vert_path,
                frag_path,
                rs_out_path,
                e.to_string()
            ));
        }
    }

    Ok(())
}
