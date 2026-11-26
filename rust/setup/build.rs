fn main() {
    println!("cargo:rerun-if-changed=payload");
    println!("cargo:rerun-if-changed=../../assets/Screenshot-installer.ico");
    embed_resource::compile("setup.rc", embed_resource::NONE)
        .manifest_required()
        .expect("embed installer resources");
}
