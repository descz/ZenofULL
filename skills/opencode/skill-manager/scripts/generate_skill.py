#!/usr/bin/env python3
"""
Generate a new skill from a template based on the skill type.
Usage: python generate_skill.py <skill-name> <skill-type>
Skill types: workflow, integration, data-processing
"""

import sys
import json
from pathlib import Path
from datetime import datetime

SCRIPT_PATH = Path(__file__).resolve()
SKILL_MANAGER_ROOT = SCRIPT_PATH.parents[1]
SKILLS_ROOT = SCRIPT_PATH.parents[2]

def create_skill_directory(skill_name):
    """Create the skill directory structure."""
    skill_path = SKILLS_ROOT / skill_name
    
    if skill_path.exists():
        print(f"[ERROR] Skill '{skill_name}' already exists at {skill_path}")
        return None
    
    # Create directories
    skill_path.mkdir(parents=True, exist_ok=True)
    (skill_path / "scripts").mkdir(exist_ok=True)
    (skill_path / "references").mkdir(exist_ok=True)
    (skill_path / "templates").mkdir(exist_ok=True)
    
    print(f"[OK] Created skill directory: {skill_path}")
    return skill_path

def get_template_content(skill_type):
    """Get the template content based on skill type."""
    template_path = SKILL_MANAGER_ROOT / "templates" / f"{skill_type}-template.md"
    
    if not template_path.exists():
        print(f"[ERROR] Template not found: {template_path}")
        return None
    
    return template_path.read_text()

def create_skill_md(skill_path, skill_name, skill_type, template_content):
    """Create the SKILL.md file for the new skill."""
    skill_md_path = skill_path / "SKILL.md"
    
    # Replace placeholders with skill name
    content = template_content.replace("[skill-name]", skill_name)
    content = content.replace("[Skill Name]", skill_name.replace("-", " ").title())
    content = content.replace("[Name of Path A]", "Primary Workflow")
    content = content.replace("[Name of Path B]", "Alternative Workflow")
    
    skill_md_path.write_text(content)
    print(f"[OK] Created SKILL.md: {skill_md_path}")

def create_example_script(skill_path):
    """Create an example script in the scripts directory."""
    script_path = skill_path / "scripts" / "example.py"
    
    example_content = '''#!/usr/bin/env python3
"""
Example script for the skill.
Replace this with your actual implementation.
"""

import sys
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

def main():
    """Main function."""
    try:
        logger.info("Script started")
        # Add your implementation here
        logger.info("Script completed successfully")
    except Exception as e:
        logger.error(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
'''
    
    script_path.write_text(example_content)
    script_path.chmod(0o755)
    print(f"[OK] Created example script: {script_path}")

def create_example_reference(skill_path):
    """Create an example reference document in the references directory."""
    ref_path = skill_path / "references" / "overview.md"
    
    example_content = '''# Overview

This is a reference document for the skill.

## Key Concepts

- **Concept 1**: Description of concept 1
- **Concept 2**: Description of concept 2

## Examples

### Example 1
[Add example 1 here]

### Example 2
[Add example 2 here]

## Resources

- [Resource 1](https://example.com)
- [Resource 2](https://example.com)
'''
    
    ref_path.write_text(example_content)
    print(f"[OK] Created example reference: {ref_path}")

def create_metadata_file(skill_path, skill_name, skill_type):
    """Create a metadata file for the skill."""
    metadata_path = skill_path / ".metadata.json"
    
    metadata = {
        "name": skill_name,
        "type": skill_type,
        "created": datetime.now().isoformat(),
        "version": "1.0.0",
        "status": "draft"
    }
    
    metadata_path.write_text(json.dumps(metadata, indent=2))
    print(f"[OK] Created metadata file: {metadata_path}")

def main():
    """Main function."""
    if len(sys.argv) < 3:
        print("Usage: python generate_skill.py <skill-name> <skill-type>")
        print("Skill types: workflow, integration, data-processing")
        sys.exit(1)
    
    skill_name = sys.argv[1]
    skill_type = sys.argv[2]
    
    # Validate skill type
    valid_types = ["workflow", "integration", "data-processing"]
    if skill_type not in valid_types:
        print(f"[ERROR] Invalid skill type: {skill_type}")
        print(f"Valid types: {', '.join(valid_types)}")
        sys.exit(1)
    
    # Create skill directory
    skill_path = create_skill_directory(skill_name)
    if not skill_path:
        sys.exit(1)
    
    # Get template content
    template_content = get_template_content(skill_type)
    if not template_content:
        sys.exit(1)
    
    # Create SKILL.md
    create_skill_md(skill_path, skill_name, skill_type, template_content)
    
    # Create example files
    create_example_script(skill_path)
    create_example_reference(skill_path)
    create_metadata_file(skill_path, skill_name, skill_type)
    
    print(f"\n[DONE] Skill '{skill_name}' generated successfully!")
    print(f"Location: {skill_path}")
    print("\nNext steps:")
    print("1. Edit SKILL.md to complete the skill definition")
    print("2. Add scripts to the scripts/ directory")
    print("3. Add references to the references/ directory")
    validator_path = SKILL_MANAGER_ROOT / "scripts" / "validate_skill.py"
    print(f"4. Run validation: python {validator_path} <skill-name>")

if __name__ == "__main__":
    main()
