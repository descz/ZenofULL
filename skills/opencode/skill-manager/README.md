# Skill Manager - Complete Documentation

## Overview

The **Skill Manager** is a comprehensive tool designed for Codex to autonomously create, structure, validate, and manage highly complex skills. It provides templates, scripts, and reference documentation to ensure that any generated skill is robust, efficient, and follows industry best practices.

## Quick Start

### Generating a New Skill

To generate a new skill using the Skill Manager, use the `generate_skill.py` script:

```bash
python ~/.config/opencode/skills/skill-manager/scripts/generate_skill.py <skill-name> <skill-type>
```

**Supported Skill Types:**
- `workflow`: For sequential, step-by-step processes
- `integration`: For skills involving external tools or APIs (e.g., Playwright, Docker Compose)
- `data-processing`: For skills that ingest, transform, and export data

### Validating a Skill

After creating or modifying a skill, validate its structure and content:

```bash
python ~/.config/opencode/skills/skill-manager/scripts/validate_skill.py <skill-name>
```

### Analyzing Skill Complexity

To understand the complexity and dependencies of a skill, run the analyzer:

```bash
python ~/.config/opencode/skills/skill-manager/scripts/skill_analyzer.py <skill-name>
```

## Directory Structure

```
skill-manager/
├── SKILL.md                          # Main instruction file for Codex
├── scripts/
│   ├── generate_skill.py             # Script to generate new skills
│   ├── validate_skill.py             # Script to validate skill structure
│   └── skill_analyzer.py             # Script to analyze skill complexity
├── references/
│   ├── skill-patterns.md             # Architectural patterns for skills
│   └── best-practices.md             # Best practices for skill creation
└── templates/
    ├── workflow-template.md          # Template for workflow-based skills
    ├── integration-template.md       # Template for integration-based skills
    └── data-processing-template.md   # Template for data processing skills
```

## Key Features

### 1. Multiple Skill Templates
The Skill Manager includes three primary templates for different skill types:
- **Workflow Template**: For skills that follow a strict, sequential process
- **Integration Template**: For skills that rely on external tools or APIs
- **Data Processing Template**: For skills that process large amounts of data

### 2. Automated Validation
The `validate_skill.py` script ensures that every generated skill meets quality standards:
- Checks for required SKILL.md structure and frontmatter
- Verifies directory structure
- Validates Python scripts for error handling and documentation
- Provides warnings for potential improvements

### 3. Complexity Analysis
The `skill_analyzer.py` script provides insights into a skill's complexity:
- Estimates total lines of code
- Counts scripts, references, and templates
- Identifies dependencies and patterns
- Generates recommendations for improvement

### 4. Progressive Disclosure
All skills follow the principle of progressive disclosure:
- High-level workflows in SKILL.md
- Detailed documentation in the `references/` directory
- Reusable assets in the `scripts/` and `templates/` directories

## Best Practices

When using the Skill Manager to create new skills, follow these principles:

1. **Keep SKILL.md Concise**: Aim for under 500 lines. Move verbose details to reference files.
2. **Robust Scripting**: Always include error handling, logging, and clear documentation.
3. **Modular Design**: Break down complex tasks into smaller, reusable components.
4. **Clear Triggers**: Define exactly when a skill should be used in its description.
5. **Comprehensive Testing**: Validate and test skills with realistic examples before deployment.

## Reference Documentation

For detailed information about skill creation, refer to:
- **`references/skill-patterns.md`**: Architectural patterns for complex skills
- **`references/best-practices.md`**: Best practices for skill design and implementation

## Examples

### Example 1: Generate a Workflow-Based Skill

```bash
python ~/.config/opencode/skills/skill-manager/scripts/generate_skill.py document-processor workflow
```

This creates a new skill for processing documents in a sequential manner.

### Example 2: Generate an Integration-Based Skill

```bash
python ~/.config/opencode/skills/skill-manager/scripts/generate_skill.py web-scraper integration
```

This creates a new skill for web scraping using tools like Playwright.

### Example 3: Generate a Data Processing Skill

```bash
python ~/.config/opencode/skills/skill-manager/scripts/generate_skill.py data-aggregator data-processing
```

This creates a new skill for ingesting, transforming, and exporting data.

## Support and Troubleshooting

If you encounter issues while using the Skill Manager:

1. **Validation Errors**: Run `validate_skill.py` to identify structural issues.
2. **Complexity Warnings**: Use `skill_analyzer.py` to understand the skill's complexity and get recommendations.
3. **Script Errors**: Check the error messages and ensure all dependencies are installed.

## Version Information

- **Skill Manager Version**: 1.0.0
- **Created**: 2026-03-26
- **Status**: Production Ready
