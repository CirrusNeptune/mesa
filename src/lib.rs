use std::collections::HashSet;
use std::env;
use std::ffi::OsStr;
use std::fmt::Debug;
use std::fs;
use std::io::{Read, Write};
use std::path::PathBuf;
use syn::visit::Visit;

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

fn needs_build(
    vert_metadata: fs::Metadata,
    frag_metadata: fs::Metadata,
    rs_path: &PathBuf,
) -> bool {
    let path = rs_path.as_path();
    if !path.exists() {
        return true;
    }
    let rs_mod = path.metadata().unwrap().modified().unwrap();
    vert_metadata.modified().unwrap() > rs_mod || frag_metadata.modified().unwrap() > rs_mod
}

fn for_each_file_ext_in_dir<F>(dir: &PathBuf, ext: &str, mut f: F) -> Result<(), String>
where
    F: FnMut(PathBuf, fs::Metadata) -> Result<(), String>,
{
    for path_ent in fs::read_dir(&dir).unwrap() {
        if path_ent.is_err() {
            continue;
        }

        let de = path_ent.unwrap();

        let metadata = de.metadata().unwrap();
        if !metadata.is_file() {
            continue;
        }
        let path = de.path();
        if path.extension().unwrap_or("".as_ref()) != ext {
            continue;
        }

        f(path, metadata)?;
    }

    Ok(())
}

fn compile_shaders(shaders_dir: &PathBuf, generated_dir: &PathBuf) -> Result<bool, String> {
    let mut compiled_shader = false;

    for_each_file_ext_in_dir(&shaders_dir, "vert", |vert_path, vert_metadata| {
        let frag_path = vert_path.with_extension("frag");
        let frag_path_path = frag_path.as_path();
        if !frag_path_path.exists() {
            return Err(format!(
                "{} does not exist",
                frag_path_path.to_str().unwrap()
            ));
        }
        let frag_metadata = frag_path_path.metadata().unwrap();
        if !frag_metadata.is_file() {
            return Err(format!(
                "{} is not a file",
                frag_path_path.to_str().unwrap()
            ));
        }

        let rs_path = generated_dir
            .join(vert_path.file_name().unwrap())
            .with_extension("rs");
        if needs_build(vert_metadata, frag_metadata, &rs_path) {
            compiled_shader = true;
            match compile_shader(vert_path, frag_path, rs_path) {
                Err(e) => return Err(e),
                _ => {}
            }
        }

        Ok(())
    })?;

    Ok(compiled_shader)
}

#[derive(Default)]
struct ObjVisitor {
    pub ids: Vec<String>,
}

impl<'ast> Visit<'ast> for ObjVisitor {
    fn visit_expr_path(&mut self, node: &'ast syn::ExprPath) {
        let segments = &node.path.segments;
        if segments.len() == 3 {
            if segments[0].ident.to_string() == "objects" && segments[2].ident.to_string() == "ASM"
            {
                self.ids.push(segments[1].ident.to_string())
            }
        }
        syn::visit::visit_expr_path(self, node);
    }
}

fn build_generated_mod(
    generated_dir: &PathBuf,
    mod_path: PathBuf,
) -> Result<HashSet<String>, String> {
    let mut gen_mod_f = fs::File::create(mod_path).unwrap();
    gen_mod_f
        .write(
            "use super::ShaderNode;\nmod objects;\npub use objects::initialize_shaders;\n"
                .as_bytes(),
        )
        .unwrap();
    let mut obj_set = HashSet::<String>::new();

    for_each_file_ext_in_dir(&generated_dir, "rs", |rs_path, _| {
        let stem = rs_path.file_stem().unwrap();
        if stem == "mod" {
            return Ok(());
        }
        gen_mod_f
            .write(format!("pub mod {};\n", stem.to_str().unwrap()).as_bytes())
            .unwrap();
        let rs_s = {
            let mut rs_f = fs::File::open(rs_path).unwrap();
            let mut rs_s = String::new();
            rs_f.read_to_string(&mut rs_s).unwrap();
            rs_s
        };
        let ast = syn::parse_file(&rs_s).unwrap();
        let mut visitor = ObjVisitor::default();
        visitor.visit_file(&ast);
        for id in visitor.ids {
            obj_set.insert(id);
        }
        Ok(())
    })?;

    Ok(obj_set)
}

fn build_objects_mod(
    objects_dir: &PathBuf,
    mod_path: PathBuf,
    obj_set: HashSet<String>,
) -> Result<(), String> {
    let mut obj_mod_f = fs::File::create(mod_path).unwrap();
    obj_mod_f
        .write("#![allow(nonstandard_style)]\nuse super::ShaderNode;\n".as_bytes())
        .unwrap();

    for obj in &obj_set {
        obj_mod_f
            .write(format!("pub mod {};\n", obj).as_bytes())
            .unwrap();
    }

    obj_mod_f
        .write(
            "\npub async fn initialize_shaders() {
    let _ = vc4_drm::tokio::join!(\n"
                .as_bytes(),
        )
        .unwrap();
    for obj in &obj_set {
        obj_mod_f
            .write(format!("        {}::ASM.initialize(),\n", obj).as_bytes())
            .unwrap();
    }
    obj_mod_f.write("    );\n}\n".as_bytes()).unwrap();

    for_each_file_ext_in_dir(&objects_dir, "rs", |rs_path, _| {
        let stem = rs_path.file_stem().unwrap();
        if stem == "mod" {
            return Ok(());
        }
        if !obj_set.contains(stem.to_str().unwrap()) {
            fs::remove_file(rs_path).ok();
        }
        Ok(())
    })?;

    Ok(())
}

pub fn build_shaders_dir(shaders_dir: &PathBuf) -> Result<(), String> {
    let generated_dir = shaders_dir.join("generated");
    let objects_dir = generated_dir.join("objects");
    fs::create_dir_all(&objects_dir).unwrap();

    let compiled_shader = compile_shaders(&shaders_dir, &generated_dir)?;
    let generated_mod_path = generated_dir.join("mod.rs");
    let objects_mod_path = objects_dir.join("mod.rs");

    if compiled_shader || !generated_mod_path.is_file() || !objects_mod_path.is_file() {
        let obj_set = build_generated_mod(&generated_dir, generated_mod_path)?;
        build_objects_mod(&objects_dir, objects_mod_path, obj_set)?;
    }

    Ok(())
}
