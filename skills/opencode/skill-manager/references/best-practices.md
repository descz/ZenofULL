# Best Practices for Skill Creation

Creating a robust, efficient, and reliable skill requires adhering to a set of best practices. This document outlines the key principles to follow when designing and implementing a new skill.

## 1. Conciseness and Clarity
- **Keep SKILL.md under 500 lines**: The main instruction file should be a high-level guide. Move detailed explanations, schemas, and complex workflows to separate files in the `references/` directory.
- **Use Imperative Language**: Write instructions clearly and directly (e.g., "Run the script", "Check the output").
- **Avoid Redundancy**: Do not repeat information that is already present in the system prompt or other reference files.

## 2. Robust Scripting
- **Error Handling**: All scripts must include comprehensive error handling (e.g., `try...except` blocks in Python).
- **Logging**: Implement logging to provide clear feedback on the script's progress and any issues encountered.
- **Modularity**: Break down complex tasks into smaller, reusable functions or scripts.
- **Dependencies**: Clearly document any required external libraries or tools in the `Prerequisites` section of the `SKILL.md`.

## 3. Progressive Disclosure
- **Layered Information**: Present information only when it is needed. Start with the high-level workflow in `SKILL.md` and reference detailed documents for specific steps.
- **Reference Files**: Use the `references/` directory for in-depth documentation, API references, database schemas, and comprehensive guides.

## 4. Validation and Testing
- **Automated Validation**: Always run the `validate_skill.py` script to ensure the skill meets structural and content requirements.
- **Manual Testing**: Test the skill's workflow and scripts with realistic examples to verify functionality and identify edge cases.

## 5. User Experience
- **Clear Triggers**: Define exactly when the skill should be used in the `description` field of the `SKILL.md` frontmatter.
- **Troubleshooting**: Include a dedicated section for common issues and their solutions to help users resolve problems quickly.
- **Examples**: Provide concrete examples of how the skill can be used to achieve specific goals.
