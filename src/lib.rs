use std::ffi::OsStr;
use std::fmt::Display;

pub fn compile_shader<S: AsRef<OsStr> + Display>(
    vert_path: S,
    frag_path: S,
    rs_out_path: S,
) -> Result<(), String> {
    use std::process::Command;

    let output = Command::new(env!("VC4_COMPILER_BIN"))
        .arg(&vert_path)
        .arg(&frag_path)
        .arg(&rs_out_path)
        .output();

    match output {
        Ok(ref out) => {
            if !out.status.success() {
                if let Ok(stderr) = String::from_utf8(output.unwrap().stderr) {
                    return Err(format!(
                        "{} {} {} {}\n{}",
                        env!("VC4_COMPILER_BIN"),
                        vert_path,
                        frag_path,
                        rs_out_path,
                        stderr
                    ));
                }
            }
        }
        Err(e) => {
            return Err(format!(
                "{} {} {} {}\n{}",
                env!("VC4_COMPILER_BIN"),
                vert_path,
                frag_path,
                rs_out_path,
                e.to_string()
            ));
        }
    }

    Ok(())
}
