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
        assert page.locator('button[data-action="new-session"]').count() == 1
        assert page.locator('button[data-workspace="memory"]').count() == 1
        assert page.locator('button[data-workspace="marketplace"]').count() == 1
        assert page.locator('button[data-workspace="squad"]').count() == 1
        page.locator('[data-sidebar-root] [aria-label="Settings"]').click()
        page.locator('[data-settings-backdrop]').wait_for()
        page.keyboard.press("Escape")
        page.locator('[data-settings-backdrop]').wait_for(state="detached")

        page.locator('button[data-workspace="memory"]').click()
        page.locator(".memory-view").wait_for()
        assert page.get_by_text("Zeno Agent Memory", exact=True).count() == 1
        assert not page.locator("[data-nav-root]").is_visible()
        assert page.locator(".memory-node").count() >= 7
        assert page.locator(".memory-node").count() >= 20
        assert page.locator(".memory-map-canvas").count() == 1
        first_node = page.locator(".memory-node").first
        first_edge = page.locator('[data-memory-edge][data-from="memory-orbit"]').first
        shape_box = first_node.locator(".memory-node-shape").bounding_box()
        old_edge_x = first_edge.get_attribute("x1")
        page.mouse.move(shape_box["x"] + shape_box["width"] / 2, shape_box["y"] + shape_box["height"] / 2)
        page.mouse.down()
        page.mouse.move(shape_box["x"] + 35, shape_box["y"] + 20)
        page.mouse.up()
        assert first_edge.get_attribute("x1") != old_edge_x
        page.screenshot(path="qa-memory.png")
        page.locator(".memory-node").nth(1).locator(".memory-node-shape").dblclick()
        page.locator(".memory-note-dialog").wait_for()
        assert page.locator(".memory-note-content").inner_text().strip()
        page.screenshot(path="qa-note.png")
        page.keyboard.press("Escape")
        page.locator(".memory-note-dialog").wait_for(state="detached")
        viewport_box = page.locator("[data-memory-viewport]").bounding_box()
        page.mouse.click(viewport_box["x"] + 500, viewport_box["y"] + 150, button="right")
        page.locator(".memory-editor-content").wait_for()
        page.locator("[data-memory-title]").fill("QA memory note")
        page.locator("[data-memory-content]").fill("# QA memory note\n\nSaved from the graph editor.")
        page.locator('[data-action="save-memory-note"]').click()
        page.locator(".memory-note-dialog").wait_for()
        assert page.get_by_text("QA memory note", exact=True).count() >= 1
        page.locator('[data-action="close-note"]').click()
        page.locator('button[data-workspace="projects"]').click()
        page.locator(".projects-view").wait_for()
        page.locator('button[data-action="new-project-main"]').first.click()
        page.locator(".project-dialog").wait_for()
        page.locator("[data-project-name]").fill("Zeno cockpit")
        page.locator('[data-project-folder]').nth(0).check()
        page.locator('[data-project-folder]').nth(2).check()
        assert page.locator("[data-project-folder-count]").inner_text() == "2 selected"
        page.locator("[data-project-form]").locator('button[type="submit"]').click()
        page.locator(".projects-view").wait_for()
        assert page.get_by_text("Zeno cockpit", exact=True).count() >= 1
        page.screenshot(path="qa-projects.png")

        page.locator('[data-project-new-chat]').first.click()
        page.locator('[data-composer-form]').wait_for()
        assert page.get_by_text("Zeno cockpit", exact=True).count() >= 1

        page.locator('button[data-workspace="marketplace"]').click()
        page.locator(".marketplace-view").wait_for()
        page.locator('button[data-workspace="squad"]').click()
        page.locator(".squad-view").wait_for()
        page.screenshot(path="qa-squad.png")
        mobile = browser.new_page(viewport={"width": 390, "height": 844}, device_scale_factor=1)
        mobile.goto((Path.cwd() / "index.html").as_uri())
        mobile.wait_for_timeout(700)
        mobile.locator('button[data-workspace="memory"]').click()
        mobile.locator(".memory-view").wait_for()
        mobile.locator(".memory-node").first.locator(".memory-node-shape").dblclick()
        note_box = mobile.locator(".memory-note-dialog").bounding_box()
        assert note_box and note_box["width"] <= 390 and note_box["height"] <= 844
        mobile.locator('[data-action="close-note"]').click()
        mobile.locator('button[data-workspace="projects"]').click()
        mobile.locator('button[data-action="new-project-main"]').first.click()
        mobile.locator(".project-dialog").wait_for()
        project_box = mobile.locator(".project-dialog").bounding_box()
        assert project_box and project_box["width"] <= 390
        mobile.close()
        browser.close()
    if errors:
        raise AssertionError("\n".join(errors))
    print("UI smoke test passed")


if __name__ == "__main__":
    main()
