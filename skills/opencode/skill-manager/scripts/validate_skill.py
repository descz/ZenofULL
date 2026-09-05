#!/usr/bin/env python3
"""
Validate a skill to ensure it meets quality standards.
Usage: python validate_skill.py <skill-name>
"""

import sys
import re
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve()
SKILLS_ROOT = SCRIPT_PATH.parents[2]

class SkillValidator:
    """Validator for skill structure and content."""
    
    def __init__(self, skill_name):
        self.skill_name = skill_name
        self.skill_path = SKILLS_ROOT / skill_name
        self.errors = []
        self.warnings = []
    
    def validate(self):
        """Run all validation checks."""
        print(f"[CHECK] Validating skill: {self.skill_name}")
        print(f"   Location: {self.skill_path}\n")
        
        # Check if skill directory exists
        if not self.skill_path.exists():
            self.errors.append(f"Skill directory not found: {self.skill_path}")
            return False
        
        # Check for required files
        self._check_skill_md()
        self._check_directory_structure()
        self._check_scripts()
        self._check_references()
        
        # Report results
        return self._report_results()
    
    def _check_skill_md(self):
        """Check if SKILL.md exists and is valid."""
        skill_md_path = self.skill_path / "SKILL.md"
        
        if not skill_md_path.exists():
            self.errors.append("SKILL.md not found")
            return
        
        try:
            content = skill_md_path.read_text()
            
            # Check for frontmatter
            if not content.startswith("---"):
                self.errors.append("SKILL.md missing YAML frontmatter")
                return
            
            # Extract frontmatter
            frontmatter_end = content.find("---", 3)
            if frontmatter_end == -1:
                self.errors.append("SKILL.md frontmatter not properly closed")
                return
            
            frontmatter = content[3:frontmatter_end]
            
            # Check for required fields
            if "name:" not in frontmatter:
                self.errors.append("SKILL.md frontmatter missing 'name' field")
            if "description:" not in frontmatter:
                self.errors.append("SKILL.md frontmatter missing 'description' field")
            
            # Check for content
            body = content[frontmatter_end + 3:].strip()
            if not body:
                self.errors.append("SKILL.md body is empty")
            
            # Check line count
            lines = body.split("\n")
            if len(lines) > 500:
                self.warnings.append(f"SKILL.md body is {len(lines)} lines (recommended: <500)")
            
            print("[OK] SKILL.md is valid")
        
        except Exception as e:
            self.errors.append(f"Error reading SKILL.md: {e}")
    
    def _check_directory_structure(self):
        """Check for standard directory structure."""
        required_dirs = ["scripts", "references", "templates"]
        found_dirs = []
        
        for dir_name in required_dirs:
            dir_path = self.skill_path / dir_name
            if dir_path.exists() and dir_path.is_dir():
                found_dirs.append(dir_name)
            else:
                self.warnings.append(f"Directory not found: {dir_name}/")
        
        if found_dirs:
            print(f"[OK] Found directories: {', '.join(found_dirs)}")
    
    def _check_scripts(self):
        """Check scripts directory and validate Python scripts."""
        scripts_dir = self.skill_path / "scripts"
        
        if not scripts_dir.exists():
            return
        
        scripts = list(scripts_dir.glob("*.py"))
        if not scripts:
            self.warnings.append("No Python scripts found in scripts/")
            return
        
        print(f"[OK] Found {len(scripts)} script(s)")
        
        for script in scripts:
            try:
                content = script.read_text()
                
                # Check for shebang
                if not content.startswith("#!"):
                    self.warnings.append(f"Script {script.name} missing shebang")
                
                # Check for docstring
                if '"""' not in content and "'''" not in content:
                    self.warnings.append(f"Script {script.name} missing docstring")
                
                # Check for error handling
                if "try:" not in content or "except" not in content:
                    self.warnings.append(f"Script {script.name} missing error handling")
            
            except Exception as e:
                self.errors.append(f"Error reading script {script.name}: {e}")
    
    def _check_references(self):
        """Check references directory."""
        references_dir = self.skill_path / "references"
        
        if not references_dir.exists():
            return
        
        references = list(references_dir.glob("*.md"))
        if not references:
            self.warnings.append("No reference files found in references/")
            return
        
        print(f"[OK] Found {len(references)} reference file(s)")
    
    def _report_results(self):
        """Report validation results."""
        print("\n" + "=" * 50)
        
        if self.errors:
            print(f"\n[ERROR] Validation failed with {len(self.errors)} error(s):")
            for i, error in enumerate(self.errors, 1):
                print(f"   {i}. {error}")
        else:
            print("\n[OK] Validation passed!")
        
        if self.warnings:
            print(f"\n[WARN] {len(self.warnings)} warning(s):")
            for i, warning in enumerate(self.warnings, 1):
                print(f"   {i}. {warning}")
        
        print("\n" + "=" * 50)
        
        return len(self.errors) == 0

def main():
    """Main function."""
    if len(sys.argv) < 2:
        print("Usage: python validate_skill.py <skill-name>")
        sys.exit(1)
    
    skill_name = sys.argv[1]
    validator = SkillValidator(skill_name)
    
    success = validator.validate()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
