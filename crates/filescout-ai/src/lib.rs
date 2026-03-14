//! FileScout AI — AI-powered features (natural language search, smart rename).
//!
//! This crate is a placeholder for future AI integration.
//! AI features are behind traits so backends can be swapped.

use std::path::{Path, PathBuf};

use anyhow::Result;
use filescout_core::FileEntry;

/// Trait for AI-powered file operations.
/// Implementations can use Claude API, OpenAI, local LLMs, etc.
#[allow(async_fn_in_trait)]
pub trait AiProvider: Send + Sync {
    /// Suggest how to organize the given files into folders.
    async fn suggest_organization(&self, files: &[FileEntry]) -> Result<Vec<OrganizationSuggestion>>;

    /// Search files using natural language (e.g., "photos from last summer").
    async fn natural_language_search(&self, query: &str, scope: &Path) -> Result<Vec<PathBuf>>;

    /// Generate a rename pattern from a natural language instruction.
    async fn generate_rename_pattern(
        &self,
        files: &[FileEntry],
        instruction: &str,
    ) -> Result<Vec<RenameSuggestion>>;
}

#[derive(Debug, Clone)]
pub struct OrganizationSuggestion {
    pub file: PathBuf,
    pub suggested_folder: String,
    pub reason: String,
}

#[derive(Debug, Clone)]
pub struct RenameSuggestion {
    pub original: PathBuf,
    pub suggested_name: String,
}
