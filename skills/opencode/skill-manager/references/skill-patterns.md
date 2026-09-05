# Skill Architectural Patterns

When designing a new skill, choosing the right architectural pattern is crucial for its success. This document outlines the most common patterns for complex skills.

## 1. The Workflow Pattern
Best for tasks that follow a strict, sequential process.

**Structure:**
- **Decision Tree**: Helps Codex decide which path to take based on user input.
- **Sequential Steps**: Clearly defined steps (Step 1, Step 2, etc.).
- **State Management**: Instructions on how to keep track of progress (e.g., saving intermediate results to a file).

**Example Use Case**: A skill that reads a document, extracts specific information, and generates a report.

## 2. The Integration Pattern
Best for skills that rely heavily on external tools or APIs (e.g., Playwright, Docker Compose).

**Structure:**
- **Setup/Teardown**: Clear instructions on how to initialize and clean up the environment.
- **Core Operations**: Modular scripts for specific actions (e.g., `login.py`, `scrape_data.py`).
- **Error Recovery**: Detailed troubleshooting for common integration failures (e.g., timeout errors, authentication failures).

**Example Use Case**: A skill that automates a web browser to scrape data from a dynamic website.

## 3. The Data Pipeline Pattern
Best for skills that process large amounts of data.

**Structure:**
- **Ingestion**: Scripts to fetch data from various sources.
- **Transformation**: Scripts to clean, filter, and format the data.
- **Export**: Scripts to save the data in the required format (e.g., CSV, JSON, Database).

**Example Use Case**: A skill that aggregates financial data from multiple APIs and generates a consolidated spreadsheet.

## 4. The Hybrid Pattern
Many complex skills will require a combination of the above patterns. For example, a web scraping skill (Integration) that also processes the scraped data (Data Pipeline) and follows a strict sequence (Workflow).

**Key Principle**: Always use **Progressive Disclosure**. Keep the main `SKILL.md` focused on the high-level workflow and delegate the complex details to specific reference files in the `references/` directory.
