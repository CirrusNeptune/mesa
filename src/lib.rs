use std::ffi::OsStr;

pub fn compile_shader<S: AsRef<OsStr>>(
    vert_path: S,
    frag_path: S,
    rs_out_path: S,
) -> Result<(), String> {
    use std::process::Command;

    let output = Command::new(env!("VC4_COMPILER_BIN"))
        .arg(vert_path)
        .arg(frag_path)
        .arg(rs_out_path)
        .output();

    match output {
        Ok(ref out) => {
            if !out.status.success() {
                if let Ok(stderr) = String::from_utf8(output.unwrap().stderr) {
                    return Err(stderr);
                }
            }
        }
        Err(e) => {
            return Err(e.to_string());
        }
    }

    Ok(())
}
