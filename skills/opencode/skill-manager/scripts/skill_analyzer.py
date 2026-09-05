#!/usr/bin/env python3
"""
Analyze skill requirements and complexity.
Usage: python skill_analyzer.py <skill-name>
"""

import sys
import json
from pathlib import Path
from collections import defaultdict

SCRIPT_PATH = Path(__file__).resolve()
SKILLS_ROOT = SCRIPT_PATH.parents[2]

class SkillAnalyzer:
    """Analyzer for skill complexity and requirements."""
    
    def __init__(self, skill_name):
        self.skill_name = skill_name
        self.skill_path = SKILLS_ROOT / skill_name
        self.analysis = {
            "name": skill_name,
            "complexity": "unknown",
            "scripts_count": 0,
            "references_count": 0,
            "templates_count": 0,
            "estimated_lines": 0,
            "dependencies": [],
            "patterns": [],
            "recommendations": []
        }
    
    def analyze(self):
        """Run analysis on the skill."""
        print(f"[ANALYZE] Analyzing skill: {self.skill_name}")
        print(f"   Location: {self.skill_path}\n")
        
        if not self.skill_path.exists():
            print(f"[ERROR] Skill directory not found: {self.skill_path}")
            return False
        
        # Analyze each component
        self._analyze_skill_md()
        self._analyze_scripts()
        self._analyze_references()
        self._analyze_templates()
        self._determine_complexity()
        self._generate_recommendations()
        
        # Report results
        return self._report_results()
    
    def _analyze_skill_md(self):
        """Analyze SKILL.md file."""
        skill_md_path = self.skill_path / "SKILL.md"
        
        if not skill_md_path.exists():
            return
        
        try:
            content = skill_md_path.read_text()
            lines = content.split("\n")
            
            self.analysis["estimated_lines"] += len(lines)
            
            # Check for patterns
            if "Workflow Decision Tree" in content:
                self.analysis["patterns"].append("workflow-based")
            if "Integration Setup" in content:
                self.analysis["patterns"].append("integration-based")
            if "Data Ingestion" in content:
                self.analysis["patterns"].append("data-processing")
            
            # Check for tool references
            if "Playwright" in content:
                self.analysis["dependencies"].append("Playwright")
            if "Compose" in content:
                self.analysis["dependencies"].append("Docker Compose")
            if "API" in content:
                self.analysis["dependencies"].append("External APIs")
            
            print("[OK] SKILL.md analyzed")
        
        except Exception as e:
            print(f"[WARN] Error analyzing SKILL.md: {e}")
    
    def _analyze_scripts(self):
        """Analyze scripts directory."""
        scripts_dir = self.skill_path / "scripts"
        
        if not scripts_dir.exists():
            return
        
        scripts = list(scripts_dir.glob("*.py"))
        self.analysis["scripts_count"] = len(scripts)
        
        total_lines = 0
        for script in scripts:
            try:
                content = script.read_text()
                total_lines += len(content.split("\n"))
                
                # Check for dependencies
                if "playwright" in content.lower():
                    self.analysis["dependencies"].append("Playwright")
                if "requests" in content.lower():
                    self.analysis["dependencies"].append("requests")
                if "pandas" in content.lower():
                    self.analysis["dependencies"].append("pandas")
                if "selenium" in content.lower():
                    self.analysis["dependencies"].append("Selenium")
            
            except Exception as e:
                print(f"[WARN] Error analyzing script: {e}")
        
        self.analysis["estimated_lines"] += total_lines
        
        if scripts:
            print(f"[OK] Found {len(scripts)} script(s) ({total_lines} lines)")
    
    def _analyze_references(self):
        """Analyze references directory."""
        references_dir = self.skill_path / "references"
        
        if not references_dir.exists():
            return
        
        references = list(references_dir.glob("*.md"))
        self.analysis["references_count"] = len(references)
        
        total_lines = 0
        for ref in references:
            try:
                content = ref.read_text()
                total_lines += len(content.split("\n"))
            
            except Exception as e:
                print(f"[WARN] Error analyzing reference: {e}")
        
        self.analysis["estimated_lines"] += total_lines
        
        if references:
            print(f"[OK] Found {len(references)} reference file(s) ({total_lines} lines)")
    
    def _analyze_templates(self):
        """Analyze templates directory."""
        templates_dir = self.skill_path / "templates"
        
        if not templates_dir.exists():
            return
        
        templates = list(templates_dir.glob("*"))
        self.analysis["templates_count"] = len(templates)
        
        if templates:
            print(f"[OK] Found {len(templates)} template file(s)")
    
    def _determine_complexity(self):
        """Determine the complexity level of the skill."""
        lines = self.analysis["estimated_lines"]
        scripts = self.analysis["scripts_count"]
        refs = self.analysis["references_count"]
        deps = len(set(self.analysis["dependencies"]))
        
        # Calculate complexity score
        score = 0
        score += min(lines / 100, 3)  # Up to 3 points for lines
        score += scripts  # 1 point per script
        score += refs  # 1 point per reference
        score += deps * 0.5  # 0.5 points per dependency
        
        if score < 5:
            self.analysis["complexity"] = "simple"
        elif score < 15:
            self.analysis["complexity"] = "moderate"
        elif score < 30:
            self.analysis["complexity"] = "complex"
        else:
            self.analysis["complexity"] = "very-complex"
    
    def _generate_recommendations(self):
        """Generate recommendations for improving the skill."""
        if self.analysis["scripts_count"] == 0:
            self.analysis["recommendations"].append("Consider adding scripts for automation")
        
        if self.analysis["references_count"] == 0:
            self.analysis["recommendations"].append("Consider adding reference documentation")
        
        if self.analysis["estimated_lines"] > 500:
            self.analysis["recommendations"].append("SKILL.md is large; consider splitting into references")
        
        if len(set(self.analysis["dependencies"])) > 5:
            self.analysis["recommendations"].append("Many dependencies; ensure clear documentation")
        
        if not self.analysis["patterns"]:
            self.analysis["recommendations"].append("Consider adding workflow patterns to SKILL.md")
    
    def _report_results(self):
        """Report analysis results."""
        print("\n" + "=" * 60)
        print(f"[REPORT] Analysis Report: {self.skill_name}")
        print("=" * 60)
        
        print(f"\nComplexity: {self.analysis['complexity'].upper()}")
        print(f"Estimated Lines: {self.analysis['estimated_lines']}")
        print(f"Scripts: {self.analysis['scripts_count']}")
        print(f"References: {self.analysis['references_count']}")
        print(f"Templates: {self.analysis['templates_count']}")
        
        if self.analysis["patterns"]:
            print(f"\nPatterns: {', '.join(self.analysis['patterns'])}")
        
        if self.analysis["dependencies"]:
            unique_deps = list(set(self.analysis["dependencies"]))
            print(f"\nDependencies: {', '.join(unique_deps)}")
        
        if self.analysis["recommendations"]:
            print(f"\nRecommendations:")
            for i, rec in enumerate(self.analysis["recommendations"], 1):
                print(f"   {i}. {rec}")
        
        print("\n" + "=" * 60)
        
        return True

def main():
    """Main function."""
    if len(sys.argv) < 2:
        print("Usage: python skill_analyzer.py <skill-name>")
        sys.exit(1)
    
    skill_name = sys.argv[1]
    analyzer = SkillAnalyzer(skill_name)
    
    success = analyzer.analyze()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
