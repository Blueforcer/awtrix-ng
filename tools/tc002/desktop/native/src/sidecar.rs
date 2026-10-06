use std::{
    fs::{self, File},
    io::{ErrorKind, Read},
    path::{Path, PathBuf},
};

fn directory_for(executable: &Path, macos: bool) -> Result<PathBuf, String> {
    let parent = executable
        .parent()
        .ok_or("Cannot find the installer directory.")?;
    if macos && parent.file_name().is_some_and(|name| name == "MacOS") {
        if let Some(contents) = parent.parent() {
            if contents.file_name().is_some_and(|name| name == "Contents") {
                if let Some(bundle) = contents.parent() {
                    if bundle
                        .extension()
                        .is_some_and(|extension| extension == "app")
                    {
                        return bundle
                            .parent()
                            .map(Path::to_path_buf)
                            .ok_or_else(|| "Cannot find the installer app directory.".into());
                    }
                }
            }
        }
    }
    Ok(parent.into())
}

pub fn directory() -> Result<PathBuf, String> {
    let executable =
        std::env::current_exe().map_err(|_| "Cannot locate the installer executable.")?;
    directory_for(&executable, cfg!(target_os = "macos"))
}

fn open_file(path: &Path, maximum: usize) -> Result<Option<(File, u64)>, String> {
    let name = path.file_name().unwrap_or_default().to_string_lossy();
    let metadata = match fs::symlink_metadata(path) {
        Ok(metadata) => metadata,
        Err(error) if error.kind() == ErrorKind::NotFound => return Ok(None),
        Err(_) => {
            return Err(format!(
                "Cannot inspect {name}. Check the path and access permissions."
            ))
        }
    };
    if !metadata.is_file() {
        return Err(format!("{name} must be a regular file."));
    }
    let file =
        File::open(path).map_err(|_| format!("Cannot read {name}. Check access permissions."))?;
    let size = file
        .metadata()
        .map_err(|_| format!("Cannot inspect {name}."))?
        .len();
    if size == 0 || size > maximum as u64 {
        return Err(format!(
            "{name} is empty or exceeds the permitted size. Replace the file and try again."
        ));
    }
    Ok(Some((file, size)))
}

fn read_open(file: File, size: u64, maximum: usize) -> Result<Vec<u8>, String> {
    let mut bytes = Vec::new();
    file.take(maximum as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "Cannot read the selected local file.")?;
    if bytes.len() as u64 != size || bytes.len() > maximum {
        return Err("The local file changed while being read. Replace it and try again.".into());
    }
    Ok(bytes)
}

pub fn validate_file(path: &Path, maximum: usize) -> Result<(), String> {
    open_file(path, maximum)?.ok_or_else(|| missing(path))?;
    Ok(())
}

pub fn read_path(path: &Path, maximum: usize) -> Result<Vec<u8>, String> {
    let (file, size) = open_file(path, maximum)?.ok_or_else(|| missing(path))?;
    read_open(file, size, maximum)
}

fn missing(path: &Path) -> String {
    format!(
        "The selected local file {} does not exist. Check its path.",
        path.display()
    )
}

pub fn read(directory: &Path, name: &str, maximum: usize) -> Result<Option<Vec<u8>>, String> {
    if Path::new(name).file_name().is_none_or(|file| file != name) {
        return Err("Invalid local installer filename.".into());
    }
    open_file(&directory.join(name), maximum)?
        .map(|(file, size)| read_open(file, size, maximum))
        .transpose()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn locates_portable_and_macos_bundle_sidecars() {
        assert_eq!(
            directory_for(Path::new("/downloads/installer"), false).unwrap(),
            Path::new("/downloads")
        );
        assert_eq!(
            directory_for(
                Path::new("/Applications/AWTRIX.app/Contents/MacOS/installer"),
                true
            )
            .unwrap(),
            Path::new("/Applications")
        );
        assert_eq!(
            directory_for(Path::new("/downloads/installer"), true).unwrap(),
            Path::new("/downloads")
        );
        assert_eq!(
            directory_for(Path::new("/downloads/MacOS/installer"), true).unwrap(),
            Path::new("/downloads/MacOS")
        );
    }

    #[test]
    fn reads_only_present_bounded_regular_files() {
        let directory = tempfile::tempdir().unwrap();
        assert_eq!(read(directory.path(), "firmware.zip", 8).unwrap(), None);
        fs::write(directory.path().join("firmware.zip"), b"firmware").unwrap();
        assert_eq!(
            read(directory.path(), "firmware.zip", 8).unwrap(),
            Some(b"firmware".to_vec())
        );
        assert!(read(directory.path(), "firmware.zip", 7).is_err());
        fs::write(directory.path().join("empty.zip"), b"").unwrap();
        assert!(read(directory.path(), "empty.zip", 8).is_err());
        fs::create_dir(directory.path().join("folder.zip")).unwrap();
        assert!(read(directory.path(), "folder.zip", 8).is_err());
        for name in ["../firmware.zip", "/firmware.zip", "", ".", ".."] {
            assert!(read(directory.path(), name, 8).is_err(), "{name}");
        }
    }

    #[test]
    fn explicit_paths_allow_spaces_and_unicode_but_never_ignore_missing_files() {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("my firmware é.zip");
        assert!(validate_file(&path, 8).is_err());
        assert!(read_path(&path, 8).is_err());
        fs::write(&path, b"firmware").unwrap();
        assert!(validate_file(&path, 8).is_ok());
        assert_eq!(read_path(&path, 8).unwrap(), b"firmware");
        assert!(validate_file(&path, 7).is_err());
        assert!(read_path(&path, 7).is_err());
        assert!(read_path(directory.path(), 8).is_err());
    }

    #[cfg(unix)]
    #[test]
    fn refuses_symlinks_including_dangling_links() {
        let directory = tempfile::tempdir().unwrap();
        let target = directory.path().join("target.zip");
        fs::write(&target, b"firmware").unwrap();
        std::os::unix::fs::symlink(&target, directory.path().join("firmware.zip")).unwrap();
        assert!(read(directory.path(), "firmware.zip", 8).is_err());
        fs::remove_file(target).unwrap();
        assert!(read(directory.path(), "firmware.zip", 8).is_err());
    }
}
