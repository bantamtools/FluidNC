#!/usr/bin/env python3
"""
Combined YAML to C++ pin definitions generator and PlatformIO pre-build hook.

Can be used in two modes:
1. Standalone tool: python3 pin_generator.py --hen-yaml ... --rooster-yaml ... --output ...
2. PlatformIO hook: Import("env") then call before_build(source, target, env)

Extracts critical pin definitions from recovery YAML files and generates 
BoardDetection_pins.inc with C++ constants and embedded recovery configs.
"""

import os
import sys
import re
import argparse
from pathlib import Path


def extract_pin_number(pin_str):
    """Extract GPIO number from string like 'gpio.40' or 'gpio.40:low'"""
    match = re.match(r'gpio\.(\d+)', str(pin_str))
    return int(match.group(1)) if match else None


def extract_section(yaml_content, section_name, end_patterns=None):
    """Extract a specific YAML section content"""
    if end_patterns is None:
        end_patterns = [r'^[a-zA-Z]', r'^#', r'^\s*$']
    
    # Look for the section start
    section_pattern = rf'^{re.escape(section_name)}:\s*$'
    section_match = re.search(section_pattern, yaml_content, re.MULTILINE)
    if not section_match:
        return None
    
    start_pos = section_match.end()
    lines = yaml_content[start_pos:].split('\n')
    
    section_lines = []
    base_indent = None
    
    for line in lines:
        # Skip empty lines at the start
        if base_indent is None and not line.strip():
            continue
            
        # Determine base indentation from first non-empty, non-comment line
        if base_indent is None and line.strip() and not line.strip().startswith('#'):
            base_indent = len(line) - len(line.lstrip())
            if base_indent == 0:  # Section content should be indented
                break
        
        # If we have base indentation, check if this line belongs to the section
        if base_indent is not None:
            current_indent = len(line) - len(line.lstrip())
            
            # Skip comments that appear outside the section structure
            if line.strip().startswith('#') and current_indent == 0:
                break
                
            if current_indent > 0 and current_indent < base_indent:
                # Less indented than section content = new section
                break
            if current_indent == 0 and line.strip() and not line.strip().startswith('#'):
                # No indentation and not empty = new top-level section
                break
        
        # Only add non-comment lines or comments that are properly indented within the section
        if not line.strip().startswith('#') or (base_indent and len(line) - len(line.lstrip()) >= base_indent):
            section_lines.append(line)
    
    return '\n'.join(section_lines)


def extract_critical_pins(yaml_content, board_name):
    """Extract critical pins from YAML content using section-aware parsing"""
    pins = {}
    
    # Extract I2C section
    i2c_section = extract_section(yaml_content, 'i2c0')
    if i2c_section:
        sda_match = re.search(r'sda_pin:\s+gpio\.(\d+)', i2c_section)
        scl_match = re.search(r'scl_pin:\s+gpio\.(\d+)', i2c_section)
        freq_match = re.search(r'frequency:\s+(\d+)', i2c_section)
        
        if sda_match and scl_match:
            pins['I2C_SDA'] = int(sda_match.group(1))
            pins['I2C_SCL'] = int(scl_match.group(1))
            pins['I2C_FREQUENCY'] = int(freq_match.group(1)) if freq_match else 400000
    
    # Extract control section
    control_section = extract_section(yaml_content, 'control')
    if control_section:
        enter_match = re.search(r'enter_pin:\s+gpio\.(\d+)', control_section)
        if enter_match:
            pins['ENTER_PIN'] = int(enter_match.group(1))
    
    # Extract encoder section
    encoder_section = extract_section(yaml_content, 'encoder')
    if encoder_section:
        a_match = re.search(r'a_pin:\s+gpio\.(\d+)', encoder_section)
        b_match = re.search(r'b_pin:\s+gpio\.(\d+)', encoder_section)
        if a_match and b_match:
            pins['ENCODER_A'] = int(a_match.group(1))
            pins['ENCODER_B'] = int(b_match.group(1))
    
    # Extract SD card section
    sdcard_section = extract_section(yaml_content, 'sdcard')
    if sdcard_section:
        sd_patterns = [
            ('SD_CLK', r'clk_pin:\s+gpio\.(\d+)'),
            ('SD_CMD', r'cmd_pin:\s+gpio\.(\d+)'),
            ('SD_D0', r'd0_pin:\s+gpio\.(\d+)'),
            ('SD_D1', r'd1_pin:\s+gpio\.(\d+)'),
            ('SD_D2', r'd2_pin:\s+gpio\.(\d+)'),
            ('SD_D3', r'd3_pin:\s+gpio\.(\d+)'),
            ('SD_CD', r'cd_pin:\s+gpio\.(\d+)'),
        ]
        
        for pin_name, pattern in sd_patterns:
            match = re.search(pattern, sdcard_section)
            if match:
                pins[pin_name] = int(match.group(1))
        
        # SD card frequency and width
        freq_match = re.search(r'frequency_hz:\s+(\d+)', sdcard_section)
        width_match = re.search(r'width:\s+(\d+)', sdcard_section)
        if freq_match:
            pins['SD_FREQUENCY'] = int(freq_match.group(1))
        if width_match:
            pins['SD_WIDTH'] = int(width_match.group(1))
    
    # Extract OLED section
    oled_section = extract_section(yaml_content, 'oled')
    if oled_section:
        addr_match = re.search(r'i2c_address:\s+(\d+)', oled_section)
        if addr_match:
            pins['OLED_ADDRESS'] = int(addr_match.group(1))
    
    # Extract extenders section (for I/O expander interrupt pin)
    extenders_section = extract_section(yaml_content, 'extenders')
    if extenders_section:
        interrupt_match = re.search(r'interrupt:\s+gpio\.(\d+)', extenders_section)
        if interrupt_match:
            pins['IO_EXPANDER_INT'] = int(interrupt_match.group(1))
    
    return pins


def extract_critical_snippets(yaml_content):
    """Extract YAML snippets for critical systems with clean formatting"""
    snippets = {}
    
    # Define the sections and their expected key patterns
    section_configs = {
        'I2C_SNIPPET': {
            'section': 'i2c0',
            'keys': ['sda_pin', 'scl_pin', 'frequency']
        },
        'ENCODER_SNIPPET': {
            'section': 'encoder', 
            'keys': ['a_pin', 'b_pin']
        },
        'CONTROL_SNIPPET': {
            'section': 'control',
            'keys': ['enter_pin', 'long_press_ms']
        },
        'SDCARD_SNIPPET': {
            'section': 'sdcard',
            'keys': ['frequency_hz', 'width', 'clk_pin', 'cmd_pin', 'd0_pin', 'd1_pin', 'd2_pin', 'd3_pin', 'cd_pin']
        },
        'OLED_SNIPPET': {
            'section': 'oled',
            'keys': ['i2c_num', 'i2c_address', 'width', 'height', 'radio_delay_ms']
        }
    }
    
    for snippet_name, config in section_configs.items():
        section_content = extract_section(yaml_content, config['section'])
        if section_content:
            # Extract key-value pairs using regex
            extracted_data = {}
            for key in config['keys']:
                pattern = rf'{re.escape(key)}:\s*(.+?)(?:\s*#.*)?$'
                match = re.search(pattern, section_content, re.MULTILINE)
                if match:
                    extracted_data[key] = match.group(1).strip()
            
            # Generate clean YAML snippet
            if extracted_data:
                lines = [f"{config['section']}:"]
                for key in config['keys']:
                    if key in extracted_data:
                        lines.append(f"  {key}: {extracted_data[key]}")
                
                snippets[snippet_name] = '\n'.join(lines)
    
    return snippets


def extract_c_axis_section(yaml_content):
    """Extract complete C axis definition from recovery YAML for G4 dwell injection.

    Returns:
        tuple: (c_axis_content, axes_start, axes_end) or (None, -1, -1) if not found
        - c_axis_content: The axes section content (just "  c:\n    ..." without "axes:" header)
        - axes_start: Position of start of "axes:" line in yaml_content
        - axes_end: Position of end of axes section content in yaml_content
    """
    # Extract the axes section content (everything under axes:, already without the header)
    axes_content = extract_section(yaml_content, 'axes')
    if not axes_content:
        return (None, -1, -1)

    # Find where "axes:" appears in the full YAML
    axes_pattern = r'^axes:\s*$'
    axes_match = re.search(axes_pattern, yaml_content, re.MULTILINE)
    if not axes_match:
        return (None, -1, -1)

    axes_start = axes_match.start()
    # axes_match.end() points to newline after "axes:"
    # extract_section returns content starting after that newline
    # So axes_end = position after newline + length of axes_content
    axes_end = axes_match.end() + 1 + len(axes_content)

    # Strip trailing whitespace and add consistent blank line for C_AXIS_YAML formatting
    axes_content = axes_content.rstrip() + '\n\n'

    return (axes_content, axes_start, axes_end)


def strip_axes_section_at_position(yaml_content, axes_start, axes_end):
    """Remove the entire axes section including comment, using already-found positions.

    Args:
        yaml_content: Full YAML content
        axes_start: Start position of 'axes:' line
        axes_end: End position of axes section content

    Returns:
        YAML content with axes section removed
    """
    # Look backwards for the comment line
    lines_before = yaml_content[:axes_start].rstrip('\n')
    comment_pattern = r'\n# Axis C reserved for G4 dwell operations\n$'
    comment_match = re.search(comment_pattern, lines_before + '\n')

    if comment_match:
        # Remove from start of comment through end of axes section
        removal_start = len(lines_before) - len(comment_match.group(0)) + 1
        return yaml_content[:removal_start] + yaml_content[axes_end:]
    else:
        # No comment found, just remove axes: line and content
        return yaml_content[:axes_start] + yaml_content[axes_end:]


def generate_header(hen_pins, rooster_pins, hen_yaml, rooster_yaml, output_path, extract_snippets=False):
    """Generate the C++ header file"""

    # Extract C axis sections once - we'll use these for both snippet and stripping
    hen_c_axis_content, hen_c_axis_start, hen_c_axis_end = extract_c_axis_section(hen_yaml)
    rooster_c_axis_content, rooster_c_axis_start, rooster_c_axis_end = extract_c_axis_section(rooster_yaml)

    header_content = f"""// AUTO-GENERATED FILE - DO NOT EDIT
// Generated from recover_hen.yaml and recover_rooster.yaml
// This file is included by BoardDetection.h

#pragma once

namespace Machine {{
namespace CriticalPins {{

    namespace Hen {{
"""

    # Add Hen pin constants
    for pin_name, pin_value in sorted(hen_pins.items()):
        header_content += f"        constexpr int {pin_name} = {pin_value};\n"

    # Add Hen YAML snippets if requested
    if extract_snippets:
        header_content += "\n        // YAML snippets for critical systems\n"
        snippets = extract_critical_snippets(hen_yaml)
        for snippet_name, snippet_content in snippets.items():
            header_content += f'        static const char {snippet_name}[] = R"({snippet_content})";\n\n'

    # Add C axis snippet for G4 dwell injection
    header_content += "\n        // C axis snippet for G4 dwell injection into user configs\n"
    header_content += "        // This is NOT included in recovery YAML below to save flash space.\n"
    header_content += "        // Recovery mode does not support G-code execution.\n"
    if hen_c_axis_content:
        header_content += f'        static const char C_AXIS_YAML[] = R"YAML({hen_c_axis_content})YAML";\n'
    else:
        header_content += '        static const char C_AXIS_YAML[] = "";\n'

    header_content += "\n        // Full recovery YAML\n"
    header_content += "        extern const char henRecoveryYAML[];\n"
    header_content += "    }\n\n"

    header_content += "    namespace Rooster {\n"

    # Add Rooster pin constants
    for pin_name, pin_value in sorted(rooster_pins.items()):
        header_content += f"        constexpr int {pin_name} = {pin_value};\n"

    # Add Rooster YAML snippets if requested
    if extract_snippets:
        header_content += "\n        // YAML snippets for critical systems\n"
        snippets = extract_critical_snippets(rooster_yaml)
        for snippet_name, snippet_content in snippets.items():
            header_content += f'        static const char {snippet_name}[] = R"({snippet_content})";\n\n'

    # Add C axis snippet for G4 dwell injection
    header_content += "\n        // C axis snippet for G4 dwell injection into user configs\n"
    header_content += "        // This is NOT included in recovery YAML below to save flash space.\n"
    header_content += "        // Recovery mode does not support G-code execution.\n"
    if rooster_c_axis_content:
        header_content += f'        static const char C_AXIS_YAML[] = R"YAML({rooster_c_axis_content})YAML";\n'
    else:
        header_content += '        static const char C_AXIS_YAML[] = "";\n'

    header_content += "\n        // Full recovery YAML\n"
    header_content += "        extern const char roosterRecoveryYAML[];\n"
    header_content += "    }\n"

    header_content += "}\n"   # Close CriticalPins namespace
    header_content += "}\n"   # Close Machine namespace
    header_content += "\n"

    # Recovery YAML strings (moved outside namespace)
    # Strip C axis section to save flash - use positions we already found
    header_content += "// Recovery configuration YAML strings\n"
    header_content += 'const char* const henRecoveryYAML = R"YAML('
    if hen_c_axis_content:
        # Strip from comment through end of C axis section
        hen_stripped = strip_axes_section_at_position(hen_yaml, hen_c_axis_start, hen_c_axis_end)
        header_content += hen_stripped
    else:
        header_content += hen_yaml
    header_content += ')YAML";\n\n'

    header_content += 'const char* const roosterRecoveryYAML = R"YAML('
    if rooster_c_axis_content:
        # Strip from comment through end of C axis section
        rooster_stripped = strip_axes_section_at_position(rooster_yaml, rooster_c_axis_start, rooster_c_axis_end)
        header_content += rooster_stripped
    else:
        header_content += rooster_yaml
    header_content += ')YAML";\n'
    
    # Write to file
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(header_content)
    
    return header_content


def generate_pins(hen_yaml_path, rooster_yaml_path, output_path, extract_snippets=False, verbose=True):
    """Core pin generation function - used by both standalone and hook modes"""
    
    # Read YAML files
    try:
        with open(hen_yaml_path, 'r') as f:
            hen_yaml_content = f.read()
        
        with open(rooster_yaml_path, 'r') as f:
            rooster_yaml_content = f.read()
    except FileNotFoundError as e:
        if verbose:
            print(f"Error: Could not read YAML file: {e}")
        raise
    
    # Extract pins
    hen_pins = extract_critical_pins(hen_yaml_content, "Hen")
    rooster_pins = extract_critical_pins(rooster_yaml_content, "Rooster")
    
    if not hen_pins or not rooster_pins:
        error_msg = "Error: Could not extract pins from YAML files"
        if verbose:
            print(error_msg)
        raise ValueError(error_msg)
    
    # Generate header
    generate_header(hen_pins, rooster_pins, hen_yaml_content, 
                    rooster_yaml_content, output_path, extract_snippets)
    
    if verbose:
        print(f"Generated {output_path}")
        print(f"Hen pins: {len(hen_pins)} constants")
        print(f"Rooster pins: {len(rooster_pins)} constants")
        if extract_snippets:
            hen_snippets = extract_critical_snippets(hen_yaml_content)
            rooster_snippets = extract_critical_snippets(rooster_yaml_content)
            print(f"Hen snippets: {len(hen_snippets)} sections")
            print(f"Rooster snippets: {len(rooster_snippets)} sections")
    
    return True


def before_build(source, target, env, script_path=None):
    """PlatformIO pre-build hook function"""

    # Paths - handle both env passed and None (for direct calls)
    if env is not None:
        project_dir = Path(env.get("PROJECT_DIR"))
        if script_path is None:
            # When called from PlatformIO, __file__ might not be available
            # Construct script path from project dir
            script_path = project_dir / "scripts" / "pin_generator.py"
    else:
        # Fallback for testing/direct calls
        try:
            project_dir = Path(__file__).parent.parent
            script_path = Path(__file__)
        except NameError:
            # __file__ not available, can't determine project dir
            raise RuntimeError("Cannot determine project directory without env parameter")
    
    data_dir = project_dir / "FluidNC" / "data"
    machine_dir = project_dir / "FluidNC" / "src" / "Machine"
    
    hen_yaml = data_dir / "recover_hen.yaml"
    rooster_yaml = data_dir / "recover_rooster.yaml"
    output_header = machine_dir / "BoardDetection_pins.inc"
    
    # Check if input files exist
    missing_files = []
    if not hen_yaml.exists():
        missing_files.append(str(hen_yaml))
    if not rooster_yaml.exists():
        missing_files.append(str(rooster_yaml))
    
    if missing_files:
        print("ERROR: Required files for pin generation are missing:")
        for file in missing_files:
            print(f"  - {file}")
        print("")
        print("SOLUTION: If BoardDetection_pins.inc is missing and build fails, manually generate it:")
        print(f"  python3 scripts/pin_generator.py \\")
        print(f"    --hen-yaml {hen_yaml} \\")
        print(f"    --rooster-yaml {rooster_yaml} \\")
        print(f"    --output {output_header} \\")
        print("    --extract-snippets")
        print("")
        
        # If header doesn't exist, this is a critical error
        if not output_header.exists():
            print("CRITICAL: BoardDetection_pins.inc is missing and cannot be generated.")
            print("This will cause compilation to fail.")
            if env is not None:
                env.Exit(1)
            else:
                sys.exit(1)
        else:
            print("Warning: Using existing BoardDetection_pins.inc (may be stale)")
        return
    
    # Check if regeneration is needed
    need_regen = True
    if output_header.exists():
        try:
            header_mtime = output_header.stat().st_mtime
            hen_mtime = hen_yaml.stat().st_mtime
            rooster_mtime = rooster_yaml.stat().st_mtime

            # Check script timestamp if path is available
            script_mtime = script_path.stat().st_mtime if script_path and script_path.exists() else 0

            # Regenerate if ANY source file is newer than the header
            if (header_mtime < hen_mtime or
                header_mtime < rooster_mtime or
                (script_mtime > 0 and header_mtime < script_mtime)):
                need_regen = True
            else:
                need_regen = False
        except OSError as e:
            print(f"Warning: Could not check file timestamps: {e}")
            need_regen = True
    
    if need_regen:
        print("Generating pin definitions from recovery YAML files...")
        
        try:
            generate_pins(hen_yaml, rooster_yaml, output_header, extract_snippets=False, verbose=True)
            print("Pin generation successful")
                
        except Exception as e:
            print(f"ERROR: Pin generation failed with exception: {e}")
            print("")
            print("Manual generation command:")
            print(f"  python3 scripts/pin_generator.py \\")
            print(f"    --hen-yaml {hen_yaml} \\")
            print(f"    --rooster-yaml {rooster_yaml} \\")
            print(f"    --output {output_header}")
            
            if not output_header.exists():
                if env is not None:
                    env.Exit(1)
                else:
                    sys.exit(1)
            else:
                print("Warning: Using existing BoardDetection_pins.inc")
    else:
        print("Pin definitions are up to date")


def main():
    """Standalone command-line interface"""
    parser = argparse.ArgumentParser(description='Extract pins from recovery YAML files')
    parser.add_argument('--hen-yaml', required=True, help='Path to hen recovery YAML')
    parser.add_argument('--rooster-yaml', required=True, help='Path to rooster recovery YAML')
    parser.add_argument('--output', required=True, help='Output header file path')
    parser.add_argument('--extract-snippets', action='store_true', 
                       help='Extract YAML snippets for critical systems')
    
    args = parser.parse_args()
    
    try:
        generate_pins(args.hen_yaml, args.rooster_yaml, args.output, 
                     extract_snippets=args.extract_snippets, verbose=True)
        return 0
    except Exception as e:
        print(f"Error: {e}")
        return 1


# PlatformIO hook setup - only run if imported by PlatformIO
if __name__ != '__main__':
    try:
        # This will only succeed if run from PlatformIO context
        Import("env")

        # Hook into the build process - run before any compilation starts
        before_build(None, None, env)

        # Also hook into buildprog for completeness
        env.AddPreAction("buildprog", before_build)

    except NameError:
        # Import("env") failed, so we're not in PlatformIO context
        # This is fine - script can still be used standalone
        pass

# Standalone execution
if __name__ == '__main__':
    sys.exit(main())