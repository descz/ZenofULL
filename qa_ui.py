from playwright.sync_api import sync_playwright
from pathlib import Path


def main():
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 900}, device_scale_factor=1)
        page.on("pageerror", lambda error: errors.append(f"pageerror: {error}"))
        page.on("console", lambda message: errors.append(f"console {message.type}: {message.text}") if message.type == "error" else None)
        page.goto((Path.cwd() / "index.html").as_uri())
        page.wait_for_load_state("networkidle")
        page.wait_for_timeout(500)
        page.screenshot(path="qa-initial.png")

        # Removed surfaces stay gone.
        assert page.locator('button[data-workspace="memory"]').count() == 1
        assert page.locator('button[data-workspace="marketplace"]').count() == 0
        assert page.locator('button[data-workspace="squad"]').count() == 0
        assert page.locator('button[data-workspace="projects"]').count() == 0
        assert page.locator('.zeno-sidebar-label').count() == 0
        assert page.locator('.zeno-sidebar-section-title').count() == 0
        assert page.locator('.oc-vibrancy-pill').count() == 0
        assert page.get_by_text("Marketplace", exact=True).count() == 0
        assert page.get_by_text("Squad", exact=True).count() == 0
        assert page.get_by_text("About Zeno", exact=True).count() == 0
        assert page.get_by_text("Project tools", exact=True).count() == 0

        # No hover vibrancy / focus rings left on buttons.
        for sel in ['[data-topbar-toggle]', 'button[data-workspace="memory"]', '[data-sidebar-root] .zeno-sidebar-tool', '[data-sidebar-root] .zeno-sidebar-footer-button', '[data-sidebar-root] .zeno-new-chat-button']:
            cls = page.locator(sel).first.get_attribute("class") or ""
            assert "hover:bg-interactive-hover" not in cls, f"{sel} still has hover bg"
            assert "focus-visible:ring" not in cls, f"{sel} still has focus ring"

        # "My Projects" label and no folders line under projects.
        assert page.locator('.zeno-project-tools span').first.text_content().strip() == "My Projects"
        assert page.locator('.zeno-project-folders').count() == 0

        # Sidebar toggle in the top bar now works: closes and reopens the sidebar.
        toggle = page.locator('[data-topbar-toggle]')
        assert toggle.count() == 1
        assert toggle.get_attribute("class").find("hover:bg-interactive-hover") == -1
        toggle.click()
        page.wait_for_timeout(320)
        assert page.locator('[data-sidebar-root] aside').get_attribute("aria-hidden") == "true"
        assert page.locator('[data-sidebar-root] aside').bounding_box()["width"] < 4
        page.screenshot(path="qa-closed.png")
        toggle.click()
        page.wait_for_timeout(320)
        assert page.locator('[data-sidebar-root] aside').get_attribute("aria-hidden") == "false"

        # Sidebar resize: content must follow the drag live.
        content_box = page.locator('[data-sidebar-content]').bounding_box()
        handle_box = page.locator('[data-action="resize-sidebar"]').bounding_box()
        page.mouse.move(handle_box["x"] + handle_box["width"] / 2, handle_box["y"] + 200)
        page.mouse.down()
        page.mouse.move(handle_box["x"] + 80, handle_box["y"] + 200, steps=8)
        page.mouse.up()
        page.wait_for_timeout(120)
        new_box = page.locator('[data-sidebar-content]').bounding_box()
        assert new_box["width"] > content_box["width"] + 40, f"resize did not follow drag: {content_box['width']} -> {new_box['width']}"

        # Chats section: renamed and collapsible.
        assert page.locator('[data-chats-toggle] span:last-child').text_content().strip() == "Chats"
        assert page.locator('[data-chats-list]').is_visible()
        page.locator('[data-chats-toggle]').click()
        assert not page.locator('[data-chats-list]').is_visible()
        assert page.locator('[data-chats-toggle]').get_attribute("aria-expanded") == "false"
        page.screenshot(path="qa-chats-collapsed.png")
        page.locator('[data-chats-toggle]').click()
        assert page.locator('[data-chats-list]').is_visible()

        # Create a project via the "+" tool: lands in chat.
        page.locator('[data-sidebar-root] .zeno-sidebar-tool[data-action="add-project"]').click()
        page.locator(".project-dialog").wait_for()
        page.locator("[data-project-name]").fill("Zeno cockpit")
        page.locator('[data-project-folder]').nth(0).check()
        page.locator('[data-project-folder]').nth(2).check()
        assert page.locator("[data-project-folder-count]").inner_text() == "2 selected"
        page.locator("[data-project-form]").locator('button[type="submit"]').click()
        page.locator("[data-chat-input]").wait_for()
        assert page.get_by_text("Zeno cockpit", exact=True).count() >= 1

        # Folder icon stays mandatory white.
        page.locator('[data-sidebar-root] .zeno-project-open').first.wait_for()
        icon_color = page.locator('[data-sidebar-root] .zeno-project-open .zeno-sidebar-nav-icon').first.evaluate(
            "el => getComputedStyle(el).color"
        )
        assert icon_color == "rgb(255, 255, 255)", f"folder icon color is {icon_color}"

        # Clicking the project opens the EDIT dialog, prefilled.
        page.locator('[data-sidebar-root] .zeno-project-open').first.click()
        page.locator(".project-dialog").wait_for()
        assert page.locator(".project-dialog h2").text_content().strip() == "Edit project"
        assert page.locator("[data-project-name]").input_value() == "Zeno cockpit"
        assert page.locator("[data-project-folder]:checked").count() == 2
        assert page.locator("[data-project-folder-count]").inner_text() == "2 selected"
        page.locator("[data-project-name]").fill("Cockpit v2")
        page.locator('[data-project-folder]').nth(4).check()
        page.locator("[data-project-form]").locator('button[type="submit"]').click()
        page.locator(".project-dialog").wait_for(state="detached")
        assert page.get_by_text("Cockpit v2", exact=True).count() >= 1
        assert page.get_by_text("Zeno cockpit", exact=True).count() == 0
        page.screenshot(path="qa-projects.png")

        # Memory page still intact.
        page.locator('button[data-workspace="memory"]').click()
        page.locator(".memory-view").wait_for()
        assert page.get_by_text("Zeno Agent Memory", exact=True).count() == 1
        assert page.locator(".memory-map-canvas").count() == 1
        page.screenshot(path="qa-memory.png")
        page.locator('[data-sidebar-root] [data-session-id]').first.click()
        page.locator("[data-chat-input]").wait_for()

        mobile = browser.new_page(viewport={"width": 390, "height": 844}, device_scale_factor=1)
        mobile.goto((Path.cwd() / "index.html").as_uri())
        mobile.wait_for_timeout(700)
        mobile.locator('button[data-workspace="memory"]').click()
        mobile.locator(".memory-view").wait_for()
        mobile.locator(".memory-node").first.locator(".memory-node-shape").dblclick()
        note_box = mobile.locator(".memory-note-dialog").bounding_box()
        assert note_box and note_box["width"] <= 390 and note_box["height"] <= 844
        mobile.close()
        browser.close()
    if errors:
        raise AssertionError("\n".join(errors))
    print("UI smoke test passed")


if __name__ == "__main__":
    main()
