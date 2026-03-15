fn main() {
    #[cfg(windows)]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("../../assets/icons/filescout.ico");
        res.set("ProductName", "FileScout");
        res.set("FileDescription", "FileScout — File Explorer for Windows");
        res.compile().expect("failed to compile Windows resources");
    }
}
