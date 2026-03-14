use std::path::PathBuf;

/// Navigation history for a single file pane.
///
/// Invariant: `entries` is never empty; `current < entries.len()`.
#[derive(Debug, Clone)]
pub struct History {
    entries: Vec<PathBuf>,
    current: usize,
}

impl History {
    pub fn new(initial: PathBuf) -> Self {
        Self {
            entries: vec![initial],
            current: 0,
        }
    }

    /// Navigate to a new path, discarding any forward history.
    pub fn navigate(&mut self, path: PathBuf) {
        self.entries.truncate(self.current + 1);
        self.entries.push(path);
        self.current = self.entries.len() - 1;
    }

    /// Go back one step. Returns the new current path, or `None` if already at start.
    pub fn back(&mut self) -> Option<&PathBuf> {
        if self.can_go_back() {
            self.current -= 1;
            Some(&self.entries[self.current])
        } else {
            None
        }
    }

    /// Go forward one step. Returns the new current path, or `None` if at end.
    pub fn forward(&mut self) -> Option<&PathBuf> {
        if self.can_go_forward() {
            self.current += 1;
            Some(&self.entries[self.current])
        } else {
            None
        }
    }

    pub fn can_go_back(&self) -> bool {
        self.current > 0
    }

    pub fn can_go_forward(&self) -> bool {
        self.current + 1 < self.entries.len()
    }

    pub fn current(&self) -> &PathBuf {
        &self.entries[self.current]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_history_has_no_back_or_forward() {
        let h = History::new(PathBuf::from("C:\\"));
        assert!(!h.can_go_back());
        assert!(!h.can_go_forward());
        assert_eq!(h.current(), &PathBuf::from("C:\\"));
    }

    #[test]
    fn navigate_enables_back() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        assert!(h.can_go_back());
        assert!(!h.can_go_forward());
        assert_eq!(h.current(), &PathBuf::from("C:\\Users"));
    }

    #[test]
    fn back_returns_previous_path() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        let prev = h.back().cloned();
        assert_eq!(prev, Some(PathBuf::from("C:\\")));
        assert_eq!(h.current(), &PathBuf::from("C:\\"));
    }

    #[test]
    fn back_enables_forward() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        h.back();
        assert!(h.can_go_forward());
    }

    #[test]
    fn forward_returns_next_path() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        h.back();
        let next = h.forward().cloned();
        assert_eq!(next, Some(PathBuf::from("C:\\Users")));
        assert_eq!(h.current(), &PathBuf::from("C:\\Users"));
    }

    #[test]
    fn navigate_clears_forward_history() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        h.navigate(PathBuf::from("C:\\Windows"));
        h.back(); // now at C:\Users, forward: C:\Windows
        h.navigate(PathBuf::from("C:\\Temp")); // new branch
        assert!(!h.can_go_forward()); // C:\Windows is gone
        assert_eq!(h.current(), &PathBuf::from("C:\\Temp"));
    }

    #[test]
    fn back_at_start_returns_none() {
        let mut h = History::new(PathBuf::from("C:\\"));
        assert_eq!(h.back(), None);
        assert_eq!(h.current(), &PathBuf::from("C:\\"));
    }

    #[test]
    fn forward_at_end_returns_none() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        assert_eq!(h.forward(), None);
    }

    #[test]
    fn multi_step_back_forward() {
        let mut h = History::new(PathBuf::from("C:\\"));
        h.navigate(PathBuf::from("C:\\Users"));
        h.navigate(PathBuf::from("C:\\Users\\Public"));

        h.back(); // → C:\Users
        h.back(); // → C:\

        assert_eq!(h.current(), &PathBuf::from("C:\\"));
        assert!(!h.can_go_back());
        assert!(h.can_go_forward());

        h.forward(); // → C:\Users
        assert_eq!(h.current(), &PathBuf::from("C:\\Users"));
        h.forward(); // → C:\Users\Public
        assert_eq!(h.current(), &PathBuf::from("C:\\Users\\Public"));
        assert!(!h.can_go_forward());
    }
}
