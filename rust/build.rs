fn main() {
    println!("cargo:rerun-if-changed=resources.rc");
    println!("cargo:rerun-if-changed=../assets/Screenshot.ico");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        embed_resource::compile("resources.rc", embed_resource::NONE)
            .manifest_required()
            .expect("compile Screenshot Windows resources");
    }
}
