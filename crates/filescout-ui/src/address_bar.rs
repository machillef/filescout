use std::path::{Path, PathBuf};

use iced::widget::{button, column, row, text, text_input};
use iced::{Alignment, Element, Length};

/// Messages emitted by the address bar component.
#[derive(Debug, Clone)]
pub enum AddressBarMessage {
    /// User edited the text in the path input.
    Edit(String),
    /// User pressed Enter to submit the typed path.
    Submit,
    /// User clicked a breadcrumb segment to navigate there.
    BreadcrumbNavigate(PathBuf),
    /// User clicked a drive button.
    DriveSelected(PathBuf),
    /// User pressed the Back button.
    Back,
    /// User pressed the Forward button.
    Forward,
}

/// Render the full address bar:
/// - Back / Forward navigation buttons
/// - Drive selector buttons (one per available drive)
/// - Editable path text input
/// - Clickable breadcrumb trail
pub fn address_bar_view<'a>(
    current_path: &'a Path,
    address_text: &'a str,
    drives: &'a [PathBuf],
    can_go_back: bool,
    can_go_forward: bool,
) -> Element<'a, AddressBarMessage> {
    let back_btn = {
        let b = button(text("←")).style(button::text);
        if can_go_back {
            b.on_press(AddressBarMessage::Back)
        } else {
            b
        }
    };

    let forward_btn = {
        let b = button(text("→")).style(button::text);
        if can_go_forward {
            b.on_press(AddressBarMessage::Forward)
        } else {
            b
        }
    };

    let nav_row = row![back_btn, forward_btn].spacing(4).align_y(Alignment::Center);

    let drive_buttons: Vec<Element<'a, AddressBarMessage>> = drives
        .iter()
        .map(|drive| {
            let label = drive
                .components()
                .next()
                .map(|c| c.as_os_str().to_string_lossy().into_owned())
                .unwrap_or_else(|| drive.display().to_string());
            let target = drive.clone();
            button(text(label))
                .on_press(AddressBarMessage::DriveSelected(target))
                .style(button::text)
                .into()
        })
        .collect();

    let drives_row = row(drive_buttons).spacing(2).align_y(Alignment::Center);

    let path_input = text_input("Enter path…", address_text)
        .on_input(AddressBarMessage::Edit)
        .on_submit(AddressBarMessage::Submit)
        .width(Length::Fill);

    let top_row = row![nav_row, drives_row, path_input]
        .spacing(8)
        .align_y(Alignment::Center)
        .padding(5)
        .width(Length::Fill);

    let crumbs = breadcrumbs(current_path);

    column![top_row, crumbs]
        .spacing(2)
        .width(Length::Fill)
        .into()
}

fn breadcrumbs<'a>(path: &'a Path) -> Element<'a, AddressBarMessage> {
    let segments = breadcrumb_segments(path);

    if segments.is_empty() {
        return row![].into();
    }

    let mut crumb_elements: Vec<Element<'a, AddressBarMessage>> = Vec::new();
    let last_idx = segments.len() - 1;

    for (i, (label, target)) in segments.into_iter().enumerate() {
        crumb_elements.push(
            button(text(label).size(13))
                .on_press(AddressBarMessage::BreadcrumbNavigate(target))
                .style(button::text)
                .padding([1, 4])
                .into(),
        );
        if i < last_idx {
            crumb_elements.push(text(" ›").size(13).into());
        }
    }

    row(crumb_elements)
        .spacing(0)
        .align_y(Alignment::Center)
        .padding([0, 5])
        .width(Length::Fill)
        .into()
}

/// Build a list of (display label, target PathBuf) for each ancestor segment.
///
/// Example for `C:\Users\machi`:
///   → [("C:\\", C:\), ("Users", C:\Users), ("machi", C:\Users\machi)]
fn breadcrumb_segments(path: &Path) -> Vec<(String, PathBuf)> {
    let mut ancestors: Vec<PathBuf> = path.ancestors().map(|a| a.to_path_buf()).collect();
    ancestors.reverse();

    ancestors
        .into_iter()
        .enumerate()
        .filter_map(|(i, p)| {
            let label = if i == 0 {
                let s = p.display().to_string();
                if s.is_empty() {
                    return None;
                }
                s
            } else {
                p.file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .filter(|s| !s.is_empty())?
            };
            Some((label, p))
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn breadcrumb_segments_root_only() {
        let segs = breadcrumb_segments(Path::new("C:\\"));
        assert_eq!(segs.len(), 1);
        assert_eq!(segs[0].0, "C:\\");
        assert_eq!(segs[0].1, PathBuf::from("C:\\"));
    }

    #[test]
    fn breadcrumb_segments_nested_path() {
        let segs = breadcrumb_segments(Path::new("C:\\Users\\machi"));
        assert_eq!(segs.len(), 3);
        assert_eq!(segs[0].0, "C:\\");
        assert_eq!(segs[1].0, "Users");
        assert_eq!(segs[1].1, PathBuf::from("C:\\Users"));
        assert_eq!(segs[2].0, "machi");
        assert_eq!(segs[2].1, PathBuf::from("C:\\Users\\machi"));
    }

    #[test]
    fn breadcrumb_segments_single_level() {
        let segs = breadcrumb_segments(Path::new("C:\\Windows"));
        assert_eq!(segs.len(), 2);
        assert_eq!(segs[0].0, "C:\\");
        assert_eq!(segs[1].0, "Windows");
    }
}
