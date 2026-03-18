//! File-system watcher subscription.
//!
//! Watches a set of directories for create / modify / remove events using the
//! `notify` crate and exposes the changes as an iced [`Subscription`].  The
//! subscription identity is derived from the sorted, deduplicated list of
//! watched paths, so iced automatically restarts the watcher whenever the set
//! changes (e.g. the user navigates to a new directory).

use std::any::TypeId;
use std::hash::Hash;
use std::path::PathBuf;
use std::time::Duration;

use iced::Subscription;
use iced::advanced::subscription::{EventStream, Hasher, Recipe, from_recipe};
use notify::{EventKind, RecursiveMode, Watcher};
use tokio::sync::mpsc as async_mpsc;

/// Internal recipe that drives the subscription.
struct WatcherRecipe {
    /// Sorted, deduplicated list of directories to watch.
    paths: Vec<PathBuf>,
}

impl Recipe for WatcherRecipe {
    type Output = PathBuf;

    fn hash(&self, state: &mut Hasher) {
        // Prefix with the TypeId so this recipe doesn't collide with others.
        TypeId::of::<Self>().hash(state);
        self.paths.hash(state);
    }

    fn stream(
        self: Box<Self>,
        _input: EventStream,
    ) -> iced::futures::stream::BoxStream<'static, PathBuf> {
        let paths = self.paths;

        let s = iced::stream::channel(16, async move |mut sender| {
            // Bridge the synchronous notify callback to the async world using a
            // tokio unbounded channel (send is infallible and sync-safe).
            let (notify_tx, mut notify_rx) = async_mpsc::unbounded_channel::<PathBuf>();
            let cb_tx = notify_tx;

            let watcher_result =
                notify::recommended_watcher(move |res: notify::Result<notify::Event>| {
                    if let Ok(event) = res {
                        match event.kind {
                            EventKind::Create(_) | EventKind::Modify(_) | EventKind::Remove(_) => {
                                for path in event.paths {
                                    // Normalise to parent dir so callers can
                                    // compare directly to `current_path`.
                                    let dir = if path.is_dir() {
                                        path
                                    } else {
                                        path.parent().map(|p| p.to_path_buf()).unwrap_or(path)
                                    };
                                    let _ = cb_tx.send(dir);
                                }
                            }
                            _ => {}
                        }
                    }
                });

            let mut watcher = match watcher_result {
                Ok(w) => w,
                Err(e) => {
                    tracing::warn!("file watcher unavailable: {e}");
                    // Park forever so the subscription stays registered but idle.
                    std::future::pending::<()>().await;
                    unreachable!()
                }
            };

            for p in &paths {
                if let Err(e) = watcher.watch(p.as_path(), RecursiveMode::NonRecursive) {
                    tracing::warn!("could not watch {}: {e}", p.display());
                }
            }

            loop {
                if let Some(dir) = notify_rx.recv().await {
                    // Drain any events that arrived in the same instant.
                    let mut changed = vec![dir];
                    while let Ok(p) = notify_rx.try_recv() {
                        changed.push(p);
                    }
                    changed.sort();
                    changed.dedup();

                    for path in changed {
                        let _ = sender.try_send(path);
                    }

                    // Debounce: discard further events for 300 ms so bulk file
                    // operations don't trigger dozens of reloads.
                    tokio::time::sleep(Duration::from_millis(300)).await;
                    while notify_rx.try_recv().is_ok() {}
                }
            }
        });

        Box::pin(s)
    }
}

/// Returns a [`Subscription`] that emits the affected directory [`PathBuf`]
/// whenever a file-system change is detected under any of `paths`.
///
/// Passing an empty `paths` list returns [`Subscription::none`].
pub fn watch_directories(paths: Vec<PathBuf>) -> Subscription<PathBuf> {
    if paths.is_empty() {
        return Subscription::none();
    }

    let mut key = paths;
    key.sort();
    key.dedup();

    from_recipe(WatcherRecipe { paths: key })
}
