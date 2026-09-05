---
name: skill-manager
description: Ultimate tool for Codex to autonomously design, generate, and manage highly complex skills. Use this skill whenever you need to create a new skill, extend an existing one, or build complex workflows involving multiple tools, scripts, and integrations.
---

# Skill Manager

This skill empowers Codex to autonomously create, structure, and validate other highly complex skills. It provides a comprehensive framework, templates, and scripts to ensure that any generated skill is robust, efficient, and follows best practices.

## Core Capabilities

The Skill Manager provides the following capabilities:
1. **Skill Architecture Design**: Frameworks for structuring complex skills with progressive disclosure.
2. **Template Utilization**: Pre-built templates for various domains (web scraping, data processing, API integration, etc.).
3. **Script Generation**: Guidelines and helper scripts for creating robust Python/Bash scripts to bundle with skills.
4. **Validation and Testing**: Automated tools to ensure the generated skills meet quality standards.

## Workflow for Creating a New Skill

When tasked with creating a new skill, follow this strict workflow:

### 1. Requirement Analysis
Analyze the user's request to determine the scope, required tools, and complexity of the new skill.
- Identify the core objective.
- Determine if the skill requires external integrations (e.g., Playwright, Compose, APIs).
- Decide on the level of freedom (High, Medium, Low) based on the task's fragility.

### 2. Architecture Planning
Plan the directory structure of the new skill. Every skill must follow the standard structure:
- `SKILL.md`: The core instruction file.
- `scripts/`: Executable code for deterministic tasks.
- `references/`: Documentation and schemas loaded as needed.
- `templates/`: Boilerplate files and assets.

*Reference: Read `references/skill-patterns.md` for architectural patterns.*

### 3. Template Selection
Choose the appropriate templates from the `templates/` directory to bootstrap the skill creation.
- For data processing: Use `templates/data-processing-template.md`.
- For web automation (e.g., Playwright): Use `templates/integration-template.md`.
- For general workflows: Use `templates/workflow-template.md`.

### 4. Resource Generation
Generate the necessary scripts and reference documents.
- Write robust, error-handling scripts in the `scripts/` directory.
- Create detailed reference documents in the `references/` directory for complex domain knowledge.
- Use the `scripts/generate_skill.py` helper to scaffold the skill if needed.

### 5. SKILL.md Construction
Draft the `SKILL.md` file for the new skill.
- **Frontmatter**: Must include a clear `name` and a comprehensive `description` that defines exactly when the skill should be triggered.
- **Body**: Write concise, imperative instructions. Use progressive disclosure by referencing external files in the `references/` directory for detailed information.

### 6. Validation
Run the validation script to ensure the new skill is correctly formatted and functional.
- Execute: `python ~/.config/opencode/skills/skill-manager/scripts/validate_skill.py <new-skill-name>`
- Fix any errors reported by the validator.

## Best Practices

- **Conciseness**: Keep `SKILL.md` under 500 lines. Move verbose details to `references/`.
- **Error Handling**: All bundled scripts must include robust error handling and logging.
- **Modularity**: Break down complex tasks into smaller, reusable scripts.
- **Documentation**: Provide clear examples within the skill's documentation.

*For more detailed best practices, read `references/best-practices.md`.*
