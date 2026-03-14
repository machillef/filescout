//! File search overlay — recursive fuzzy file search (Ctrl+F).

use std::path::PathBuf;

use filescout_core::SearchResult;
use iced::widget::{button, column, container, row, scrollable, text, text_input};
use iced::{Background, Color, Element, Length, Theme};

/// Maximum number of results to show in the search overlay.
pub const MAX_SEARCH_RESULTS: usize = 200;

/// Messages emitted by the search overlay UI or the background search task.
#[derive(Debug, Clone)]
pub enum SearchMessage {
    QueryChanged(String),
    ExtFilterChanged(String),
    /// Background search task completed with new results.
    ResultsLoaded(Vec<SearchResult>),
    MoveUp,
    MoveDown,
    /// Navigate to the keyboard-selected result (Enter key).
    Navigate,
    /// Navigate to a specific result (mouse click).
    NavigateTo(PathBuf),
    Close,
}

/// Live state for the search overlay.
pub struct SearchState {
    /// Directory the search starts from (active pane's current path when opened).
    pub root: PathBuf,
    /// Current query string entered by the user.
    pub query: String,
    /// Optional extension filter (e.g. `"rs"`; empty = no filter).
    pub ext_filter: String,
    /// Latest search results.
    pub results: Vec<SearchResult>,
    /// Index of the keyboard-selected result row.
    pub selected: usize,
    /// Whether a background search task is still running.
    pub loading: bool,
}

impl SearchState {
    pub fn new(root: PathBuf) -> Self {
        Self {
            root,
            query: String::new(),
            ext_filter: String::new(),
            results: Vec::new(),
            selected: 0,
            loading: false,
        }
    }

    pub fn move_up(&mut self) {
        self.selected = self.selected.saturating_sub(1);
    }

    pub fn move_down(&mut self) {
        if !self.results.is_empty() {
            self.selected = (self.selected + 1).min(self.results.len() - 1);
        }
    }

    /// Returns the path of the currently keyboard-selected result, if any.
    pub fn selected_path(&self) -> Option<&PathBuf> {
        self.results.get(self.selected).map(|r| &r.path)
    }

    /// `true` when the query is non-empty and a search should be triggered.
    pub fn should_search(&self) -> bool {
        !self.query.is_empty()
    }

    /// Returns the extension filter as `Some(&str)` when non-empty, else `None`.
    pub fn ext_filter_opt(&self) -> Option<&str> {
        if self.ext_filter.is_empty() { None } else { Some(&self.ext_filter) }
    }
}

fn backdrop_style(_theme: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(Color::from_rgba(0.0, 0.0, 0.0, 0.7))),
        ..Default::default()
    }
}

/// Renders the search overlay as a full-screen dimmed backdrop with a search panel.
pub fn search_view(state: &SearchState) -> Element<'_, SearchMessage> {
    let query_input = text_input("Search file names…", &state.query)
        .on_input(SearchMessage::QueryChanged)
        .padding(10)
        .size(15);

    let ext_input = text_input("ext (e.g. rs)", &state.ext_filter)
        .on_input(SearchMessage::ExtFilterChanged)
        .padding(10)
        .size(14)
        .width(150);

    let header = row![query_input, ext_input].spacing(8);

    let status: Element<'_, SearchMessage> = if state.loading {
        text("Searching…").size(12).into()
    } else if state.query.is_empty() {
        text("Type to search from the current directory").size(12).into()
    } else if state.results.is_empty() {
        text("No results").size(12).into()
    } else {
        text(format!("{} result(s)", state.results.len())).size(12).into()
    };

    let items: Vec<Element<'_, SearchMessage>> = state
        .results
        .iter()
        .enumerate()
        .map(|(i, result)| {
            let style = if i == state.selected { button::primary } else { button::text };
            let display = result.path.display().to_string();
            let path = result.path.clone();
            button(text(display).size(13))
                .on_press(SearchMessage::NavigateTo(path))
                .style(style)
                .width(Length::Fill)
                .padding([4, 12])
                .into()
        })
        .collect();

    let list = scrollable(column(items).spacing(2).width(Length::Fill)).height(340);

    let panel = container(column![header, status, list].spacing(6))
        .width(720)
        .padding(12)
        .style(container::rounded_box);

    container(panel).center(Length::Fill).style(backdrop_style).into()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state_with_results(n: usize) -> SearchState {
        let mut s = SearchState::new(PathBuf::from("C:\\"));
        s.results = (0..n)
            .map(|i| SearchResult { path: PathBuf::from(format!("C:\\file{}.rs", i)), score: (n - i) as i64 })
            .collect();
        s
    }

    #[test]
    fn new_state_has_empty_query_and_no_results() {
        let s = SearchState::new(PathBuf::from("C:\\"));
        assert!(s.query.is_empty());
        assert!(s.ext_filter.is_empty());
        assert!(s.results.is_empty());
        assert_eq!(s.selected, 0);
        assert!(!s.loading);
    }

    #[test]
    fn move_up_saturates_at_zero() {
        let mut s = state_with_results(3);
        s.selected = 0;
        s.move_up();
        assert_eq!(s.selected, 0);
    }

    #[test]
    fn move_down_advances_selection() {
        let mut s = state_with_results(3);
        s.move_down();
        assert_eq!(s.selected, 1);
    }

    #[test]
    fn move_down_clamps_at_last_index() {
        let mut s = state_with_results(3);
        s.selected = 2;
        s.move_down();
        assert_eq!(s.selected, 2);
    }

    #[test]
    fn move_down_on_empty_results_is_noop() {
        let mut s = SearchState::new(PathBuf::from("C:\\"));
        s.move_down();
        assert_eq!(s.selected, 0);
    }

    #[test]
    fn selected_path_returns_none_when_empty() {
        let s = SearchState::new(PathBuf::from("C:\\"));
        assert!(s.selected_path().is_none());
    }

    #[test]
    fn selected_path_returns_current_selection() {
        let s = state_with_results(3);
        assert_eq!(s.selected_path().unwrap(), &PathBuf::from("C:\\file0.rs"));
    }

    #[test]
    fn should_search_false_when_query_empty() {
        let s = SearchState::new(PathBuf::from("C:\\"));
        assert!(!s.should_search());
    }

    #[test]
    fn should_search_true_when_query_set() {
        let mut s = SearchState::new(PathBuf::from("C:\\"));
        s.query = "foo".into();
        assert!(s.should_search());
    }

    #[test]
    fn ext_filter_opt_none_when_empty() {
        let s = SearchState::new(PathBuf::from("C:\\"));
        assert!(s.ext_filter_opt().is_none());
    }

    #[test]
    fn ext_filter_opt_some_when_set() {
        let mut s = SearchState::new(PathBuf::from("C:\\"));
        s.ext_filter = "rs".into();
        assert_eq!(s.ext_filter_opt(), Some("rs"));
    }
}
